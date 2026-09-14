"""HTTP fallback and listener-state checks against standalone native firmware."""

import json
import os
import socket
import subprocess
import time
from contextlib import contextmanager

import pytest
import requests

from .conftest import get_native_binary_path


def seed_server_certificate(filesystem, directory):
    """Create a short-ID test certificate without depending on the upload API."""
    certificate = directory / "server.pem"
    key = directory / "server.key"
    subprocess.run(
        [
            "openssl", "req", "-new", "-x509", "-newkey", "ec",
            "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes",
            "-keyout", str(key), "-out", str(certificate),
            "-days", "2", "-set_serial", "0x1234", "-subj", "/CN=localhost",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    certificates = filesystem / "certificates"
    certificates.mkdir(parents=True)
    (certificates / "1234.json").write_text(
        json.dumps({
            "id": "1234",
            "name": "dummy-server",
            "certificate": certificate.read_text(encoding="ascii"),
            "key": key.read_text(encoding="ascii"),
        }),
        encoding="ascii",
    )


@contextmanager
def occupied_port():
    """Reserve a loopback listener so firmware startup on that port must fail."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        yield listener.getsockname()[1]


@contextmanager
def native_server(directory, http_port, https_port):
    runtime = directory / "runtime"
    runtime.mkdir()
    filesystem = runtime / "epoxyfsdata"
    seed_server_certificate(filesystem, directory)
    environment = os.environ.copy()
    environment["EPOXY_FS_ROOT"] = str(filesystem)
    log_path = directory / "native.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            [
                str(get_native_binary_path()),
                "--set-config", f"www_http_port={http_port}",
                "--set-config", f"www_https_port={https_port}",
                "--set-config", "www_certificate_id=1234",
            ],
            cwd=runtime,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            yield process, log_path
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def wait_for_response(process, url):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        assert process.poll() is None, "native firmware exited during startup"
        try:
            return requests.get(url, timeout=2, allow_redirects=False)
        except requests.RequestException:
            time.sleep(0.1)
    pytest.fail("HTTP listener did not respond before the startup deadline")


@pytest.mark.timeout(90)
def test_failed_https_listener_serves_http_fallback(tmp_path):
    with occupied_port() as https_port:
        with occupied_port() as http_port:
            pass  # Release only the HTTP port before starting the firmware.
        with native_server(tmp_path, http_port, https_port) as (process, _):
            response = wait_for_response(process, f"http://127.0.0.1:{http_port}/status")
            assert response.status_code == 200
            assert isinstance(response.json(), dict)


@pytest.mark.timeout(90)
def test_http_fallback_peer_advertises_selected_listener(tmp_path):
    with occupied_port() as https_port:
        with occupied_port() as http_port:
            pass
        with native_server(tmp_path, http_port, https_port) as (process, _):
            base = f"http://127.0.0.1:{http_port}"
            response = wait_for_response(process, f"{base}/loadsharing/peers")
            assert response.status_code == 200
            local = next(peer for peer in response.json() if peer["isLocal"])
            assert local["online"]
            assert local["url"] == f"http://{local['host']}:{http_port}"


@pytest.mark.timeout(90)
def test_both_listener_failures_report_inactive_server(tmp_path):
    with occupied_port() as https_port, occupied_port() as http_port:
        with native_server(tmp_path, http_port, https_port) as (process, log_path):
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                assert process.poll() is None, "native firmware exited during startup"
                output = log_path.read_text(encoding="utf-8", errors="replace")
                assert "Server started" not in output, "failed listeners reported as started"
                if "Server failed to start" in output:
                    return
                time.sleep(0.1)
            pytest.fail("native firmware did not report listener failure")
