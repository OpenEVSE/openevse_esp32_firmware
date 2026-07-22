"""Certificate API persistence tests against the native firmware.

The generated hierarchy mirrors Let's Encrypt's ECDSA deployment shape: a
P-256 server key and leaf certificate, a P-384 issuing intermediate, and a
P-384 cross-signed root beneath a separately trusted P-384 root. The test
passes the complete served chain and private key through the HTTP API, then
restarts the firmware from the same filesystem to prove that acknowledgement
implies durable, reloadable state. Deletion must also survive restart without
leaving a published partial record or stale temporary file.

The native Mongoose/OpenSSL backend requires certificate filenames, whereas
the ESP32 Mongoose/mbedTLS backend consumes the in-memory PEM strings returned
by CertificateStore. TLS listener startup is therefore covered by focused
startup tests and the ESP32 target build rather than asserted through this
native integration process.

No production key material is used. Failures cover request parsing, key or
certificate validation, partial persistence, reload, deletion, and stale
temporary files.
"""

import json
import os
import socket
import subprocess
import time
from pathlib import Path

import pytest
import requests

from .conftest import get_native_binary_path


def run_openssl(directory: Path, *arguments: str) -> None:
    subprocess.run(
        ["openssl", *arguments],
        cwd=directory,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def write_extension(path: Path, content: str) -> None:
    path.write_text(content, encoding="ascii")


def generate_ecdsa_chain(directory: Path) -> tuple[Path, Path]:
    root_key = directory / "root-x2.key"
    root_cert = directory / "root-x2.pem"
    cross_key = directory / "root-ye.key"
    cross_csr = directory / "root-ye.csr"
    cross_cert = directory / "root-ye-cross.pem"
    issuer_key = directory / "ye1.key"
    issuer_csr = directory / "ye1.csr"
    issuer_cert = directory / "ye1.pem"
    leaf_key = directory / "leaf.key"
    leaf_csr = directory / "leaf.csr"
    leaf_cert = directory / "leaf.pem"

    run_openssl(directory, "ecparam", "-name", "secp384r1", "-genkey", "-noout", "-out", root_key.name)
    run_openssl(
        directory,
        "req",
        "-new",
        "-x509",
        "-key",
        root_key.name,
        "-out",
        root_cert.name,
        "-days",
        "2",
        "-sha384",
        "-subj",
        "/CN=Integration Root X2/O=OpenEVSE Test",
        "-addext",
        "basicConstraints=critical,CA:TRUE,pathlen:2",
        "-addext",
        "keyUsage=critical,keyCertSign,cRLSign",
    )

    write_extension(
        directory / "root-ye.ext",
        "basicConstraints=critical,CA:TRUE,pathlen:1\n"
        "keyUsage=critical,keyCertSign,cRLSign\n"
        "subjectKeyIdentifier=hash\n"
        "authorityKeyIdentifier=keyid,issuer\n",
    )
    run_openssl(directory, "ecparam", "-name", "secp384r1", "-genkey", "-noout", "-out", cross_key.name)
    run_openssl(directory, "req", "-new", "-key", cross_key.name, "-out", cross_csr.name, "-subj", "/CN=Integration Root YE/O=OpenEVSE Test")
    run_openssl(
        directory,
        "x509",
        "-req",
        "-in",
        cross_csr.name,
        "-CA",
        root_cert.name,
        "-CAkey",
        root_key.name,
        "-CAcreateserial",
        "-out",
        cross_cert.name,
        "-days",
        "2",
        "-sha384",
        "-extfile",
        "root-ye.ext",
    )

    write_extension(
        directory / "ye1.ext",
        "basicConstraints=critical,CA:TRUE,pathlen:0\n"
        "keyUsage=critical,keyCertSign,cRLSign\n"
        "subjectKeyIdentifier=hash\n"
        "authorityKeyIdentifier=keyid,issuer\n",
    )
    run_openssl(directory, "ecparam", "-name", "secp384r1", "-genkey", "-noout", "-out", issuer_key.name)
    run_openssl(directory, "req", "-new", "-key", issuer_key.name, "-out", issuer_csr.name, "-subj", "/CN=Integration YE1/O=OpenEVSE Test")
    run_openssl(
        directory,
        "x509",
        "-req",
        "-in",
        issuer_csr.name,
        "-CA",
        cross_cert.name,
        "-CAkey",
        cross_key.name,
        "-CAcreateserial",
        "-out",
        issuer_cert.name,
        "-days",
        "2",
        "-sha384",
        "-extfile",
        "ye1.ext",
    )

    write_extension(
        directory / "leaf.ext",
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=IP:127.0.0.1,DNS:localhost\n"
        "subjectKeyIdentifier=hash\n"
        "authorityKeyIdentifier=keyid,issuer\n",
    )
    run_openssl(directory, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", leaf_key.name)
    run_openssl(directory, "req", "-new", "-key", leaf_key.name, "-out", leaf_csr.name, "-subj", "/CN=localhost/O=OpenEVSE Test")
    run_openssl(
        directory,
        "x509",
        "-req",
        "-in",
        leaf_csr.name,
        "-CA",
        issuer_cert.name,
        "-CAkey",
        issuer_key.name,
        "-CAcreateserial",
        "-out",
        leaf_cert.name,
        "-days",
        "2",
        "-sha256",
        "-extfile",
        "leaf.ext",
    )

    chain = directory / "fullchain.pem"
    chain.write_text(
        leaf_cert.read_text(encoding="ascii")
        + issuer_cert.read_text(encoding="ascii")
        + cross_cert.read_text(encoding="ascii"),
        encoding="ascii",
    )
    return chain, leaf_key


def wait_for_url(url: str, *, verify: bool | str, timeout: float = 90) -> requests.Response:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            response = requests.get(url, timeout=2, verify=verify)
            if response.status_code == 200:
                return response
            last_error = RuntimeError(f"HTTP {response.status_code}")
        except requests.RequestException as error:
            last_error = error
        time.sleep(0.25)
    raise AssertionError(f"{url} did not become ready: {last_error}")


def stop_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def unused_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


@pytest.mark.timeout(240)
def test_ecdsa_certificate_upload_and_delete_survive_restart(evse_instance, tmp_path):
    del evse_instance  # The session fixture ensures the native build prerequisites are available.

    chain_path, key_path = generate_ecdsa_chain(tmp_path)
    payload = {
        "name": "integration-ecdsa",
        "certificate": chain_path.read_text(encoding="ascii"),
        "key": key_path.read_text(encoding="ascii"),
    }
    encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    assert len(encoded) <= 7 * 1024, "ECDSA upload must retain parser headroom"

    binary = get_native_binary_path()
    runtime = tmp_path / "runtime"
    runtime.mkdir()
    filesystem = runtime / "epoxyfsdata"
    log_path = tmp_path / "native.log"
    process = None
    http_port = unused_tcp_port()
    https_port = unused_tcp_port()
    while https_port == http_port:
        https_port = unused_tcp_port()

    def start() -> subprocess.Popen[bytes]:
        environment = os.environ.copy()
        environment["EPOXY_FS_ROOT"] = str(filesystem)
        with log_path.open("ab") as log:
            return subprocess.Popen(
                [
                    str(binary),
                    "--set-config",
                    f"www_http_port={http_port}",
                    "--set-config",
                    f"www_https_port={https_port}",
                ],
                cwd=runtime,
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
            )

    http_base = f"http://127.0.0.1:{http_port}"
    try:
        process = start()
        wait_for_url(f"{http_base}/config", verify=False)

        uploaded = requests.post(f"{http_base}/certificates", json=payload, timeout=15)
        assert uploaded.status_code == 200, uploaded.text
        certificate_id = uploaded.json()["id"]

        listed = requests.get(f"{http_base}/certificates", timeout=10)
        assert listed.status_code == 200
        assert certificate_id in {certificate["id"] for certificate in listed.json()}
        assert not list(runtime.rglob("*.tmp"))

        stop_process(process)
        process = start()
        wait_for_url(f"{http_base}/status", verify=False)

        listed = requests.get(f"{http_base}/certificates", timeout=10)
        assert listed.status_code == 200
        assert certificate_id in {certificate["id"] for certificate in listed.json()}

        deleted = requests.delete(
            f"{http_base}/certificates/{certificate_id}",
            timeout=10,
        )
        assert deleted.status_code == 200, deleted.text

        stop_process(process)
        process = start()
        wait_for_url(f"{http_base}/status", verify=False)
        listed = requests.get(f"{http_base}/certificates", timeout=10)
        assert listed.status_code == 200
        assert listed.json() == []
        assert not list(runtime.rglob("*.tmp"))
    finally:
        stop_process(process)
