"""Standalone native certificate persistence and rollback regressions.

Run this module directly with pytest; no emulator or mDNS fixtures are needed.
Certificates and keys are generated locally with small dummy serials/IDs so
these tests exercise transactions independently of ID formatting.
"""

import json
import os
import socket
import subprocess
import time
from pathlib import Path

import pytest
import requests


def certificate_payload(directory, serial, *, client=False):
    key = directory / f"dummy-{serial}.key"
    cert = directory / f"dummy-{serial}.pem"
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt",
         "ec_paramgen_curve:prime256v1", "-nodes", "-keyout", str(key),
         "-out", str(cert), "-days", "2", "-set_serial", str(serial),
         "-subj", "/CN=example.invalid", "-addext", "basicConstraints=critical,CA:TRUE"],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    payload = {"id": str(serial), "name": f"dummy-{serial}",
               "certificate": cert.read_text(encoding="ascii")}
    if client:
        payload["key"] = key.read_text(encoding="ascii")
    return payload


def chain_payload(directory):
    """P-256 leaf, P-384 issuer and cross-signed root, beneath a P-384 root."""
    def openssl(*args):
        subprocess.run(["openssl", *args], cwd=directory, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    for name, serial, signer, curve, pathlen in (
        ("root", 1, None, "secp384r1", 2),
        ("cross", 2, "root", "secp384r1", 1),
        ("issuer", 3, "cross", "secp384r1", 0),
        ("leaf", 4, "issuer", "prime256v1", None),
    ):
        openssl("ecparam", "-name", curve, "-genkey", "-noout", "-out", f"{name}.key")
        openssl("req", "-new", "-key", f"{name}.key", "-out", f"{name}.csr",
                "-subj", f"/CN=dummy-{name}.example.invalid")
        extensions = (
            f"basicConstraints=critical,CA:TRUE,pathlen:{pathlen}\n"
            "keyUsage=critical,keyCertSign,cRLSign\n" if pathlen is not None else
            "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n"
            "extendedKeyUsage=serverAuth\nsubjectAltName=DNS:example.invalid\n"
        )
        (directory / f"{name}.ext").write_text(extensions, encoding="ascii")
        signing = (["-signkey", f"{name}.key"] if signer is None else
                   ["-CA", f"{signer}.pem", "-CAkey", f"{signer}.key"])
        openssl("x509", "-req", "-in", f"{name}.csr", *signing,
                "-set_serial", str(serial), "-out", f"{name}.pem", "-days", "2",
                "-sha256" if name == "leaf" else "-sha384", "-extfile", f"{name}.ext")
    chain = "".join((directory / f"{name}.pem").read_text(encoding="ascii")
                    for name in ("leaf", "issuer", "cross"))
    payload = {"id": "4", "name": "dummy-ecdsa-chain", "certificate": chain,
               "key": (directory / "leaf.key").read_text(encoding="ascii")}
    assert len(json.dumps(payload).encode("utf-8")) <= 7 * 1024
    return payload


def fail_next_array_allocation(native, directory):
    library = directory / "fail-nothrow-new.so"
    source = Path(__file__).with_name("fail_nothrow_new.cpp")
    subprocess.run(["c++", "-shared", "-fPIC", str(source), "-ldl", "-o", str(library)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    marker = directory / "fail-next-array"
    native.env["LD_PRELOAD"] = str(library)
    native.env["OPENEVSE_FAIL_NOTHROW_NEW_ARRAY_MARKER"] = str(marker)
    return marker


@pytest.fixture
def native(tmp_path):
    binary = Path(os.environ.get("NATIVE_BINARY_PATH", str(
        Path(__file__).resolve().parents[2] / ".pio/build/native_openevse/program")))
    assert binary.is_file(), "Build native_openevse before running these tests"
    runtime = tmp_path / "runtime"
    runtime.mkdir()
    filesystem = runtime / "epoxyfsdata"
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    environment = os.environ.copy()
    environment["EPOXY_FS_ROOT"] = str(filesystem)
    process = None

    class Native:
        files = filesystem / "certificates"
        base = f"http://127.0.0.1:{port}"
        env = environment

        def stop(self):
            nonlocal process
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)

        def start(self):
            nonlocal process
            self.stop()
            with (tmp_path / "native.log").open("ab") as log:
                process = subprocess.Popen(
                    [str(binary), "--set-config", f"www_http_port={port}"],
                    cwd=runtime, env=environment, stdout=log, stderr=subprocess.STDOUT,
                )
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                assert process.poll() is None, "Native firmware exited before readiness"
                try:
                    if self.get("/config").status_code == 200:
                        return
                except requests.RequestException:
                    pass
                time.sleep(0.1)
            pytest.fail("Native firmware did not become ready")

        def get(self, path):
            return requests.get(self.base + path, timeout=3)

        def upload(self, payload):
            return requests.post(self.base + "/certificates", json=payload, timeout=10)

        def delete(self, serial):
            return requests.delete(self.base + f"/certificates/{serial}", timeout=10)

        def ids(self):
            response = self.get("/certificates")
            assert response.status_code == 200
            return {record["id"] for record in response.json()}

    instance = Native()
    try:
        yield instance
    finally:
        instance.stop()


def test_root_upload_storage_failure_preserves_active_state(native, tmp_path):
    first = certificate_payload(tmp_path, 1)
    second = certificate_payload(tmp_path, 2)
    native.start()
    assert native.upload(first).status_code == 200
    trust = native.get("/certificates/root").text
    # An occupied destination forces persistence to fail on both backends.
    blocked = native.files / "2.json"
    blocked.mkdir()
    (blocked / "occupied").write_text("dummy", encoding="ascii")
    assert native.upload(second).status_code == 400
    assert native.ids() == {"1"}
    assert native.get("/certificates/root").text == trust
    assert not list(native.files.glob("*.tmp"))
    native.start()
    assert native.ids() == {"1"}


def test_stale_temporary_record_is_discarded_on_restart(native, tmp_path):
    payload = certificate_payload(tmp_path, 1)
    native.files.mkdir(parents=True)
    (native.files / "1.json.tmp").write_text(json.dumps(payload), encoding="ascii")
    native.start()
    assert native.ids() == set()
    assert not list(native.files.glob("*.tmp"))


def test_ecdsa_chain_upload_and_delete_survive_restart(native, tmp_path):
    payload = chain_payload(tmp_path)
    native.start()
    assert native.upload(payload).status_code == 200
    assert native.ids() == {"4"}
    assert not list(native.files.glob("*.tmp"))
    native.start()
    assert native.ids() == {"4"}
    fetched = native.get("/certificates/4")
    assert fetched.status_code == 200
    assert fetched.json()["certificate"] == payload["certificate"]
    assert native.delete("4").status_code == 200
    native.start()
    assert native.ids() == set()
    assert native.delete("4").status_code == 404


@pytest.mark.parametrize("failure", ["allocation", "storage"])
def test_root_delete_failure_preserves_active_state(native, tmp_path, failure):
    marker = fail_next_array_allocation(native, tmp_path)
    payloads = [certificate_payload(tmp_path, serial) for serial in (1, 2)]
    native.start()
    for payload in payloads:
        assert native.upload(payload).status_code == 200
    trust = native.get("/certificates/root").text
    record = native.files / "1.json"
    saved = record.read_bytes()
    if failure == "allocation":
        marker.touch()
    else:
        record.unlink()
        record.mkdir()
        (record / "occupied").write_text("dummy", encoding="ascii")
    assert native.delete("1").status_code == 404
    if failure == "allocation":
        assert not marker.exists(), "Allocation hook was not exercised"
        assert record.read_bytes() == saved
    assert native.ids() == {"1", "2"}
    assert native.get("/certificates/root").text == trust
    if failure == "storage":
        (record / "occupied").unlink()
        record.rmdir()
        record.write_bytes(saved)
    native.start()
    assert native.ids() == {"1", "2"}


def test_root_upload_allocation_failure_preserves_active_state(native, tmp_path):
    marker = fail_next_array_allocation(native, tmp_path)
    first = certificate_payload(tmp_path, 1)
    second = certificate_payload(tmp_path, 2)
    native.start()
    assert native.upload(first).status_code == 200
    trust = native.get("/certificates/root").text
    marker.touch()
    assert native.upload(second).status_code == 400
    assert not marker.exists(), "Allocation hook was not exercised"
    assert native.ids() == {"1"}
    assert native.get("/certificates/root").text == trust
    assert not (native.files / "2.json").exists()
