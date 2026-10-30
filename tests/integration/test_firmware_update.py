"""
Regression tests for the web firmware upload endpoint (POST /update).

Issue OpenEVSE/openevse_esp32_firmware#1359: after 71e8ed3 (the ArduinoMongoose v7
migration) firmware uploads from the web app were rejected with "The charger
didn't accept the change". Signed release builds (REQUIRE_SIGNED_OTA) verify the
image against the size declared to Update.begin(): the verifier hashes
(declared - 512) bytes and treats the last 512 as the signature. A multipart
upload declares the whole multipart body length, including boundary framing,
so the declared size is larger than the image.

Only the native binary is needed (no emulator, Docker or PTY bridge). The native
Update stub in libraries/EpoxyMockWiFi/Update.h does not verify signatures, so
the declared-size check reads the firmware's "Update Start" log line instead.

Each test starts its own firmware process because a successful upload schedules
restart_system().
"""

import os
import re
import socket
import subprocess
import time
from contextlib import contextmanager
from pathlib import Path

import pytest
import requests


UPLOAD_TIMEOUT = 30

# Not a multiple of the 4 KiB flash sector size, so the upload cannot end on a
# sector boundary. The raw-body path has a zero-length write hazard that only
# shows up at exactly the declared size (see handleUpdateUpload()).
FIRMWARE_SIZE = 300_001

UPDATE_START = re.compile(r"^Update Start: (.*) (\d+)$", re.MULTILINE)


def firmware_image():
    """Deterministic stand-in for a firmware binary. The native stub never parses it."""
    block = bytes(range(256))
    return (block * (FIRMWARE_SIZE // len(block) + 1))[:FIRMWARE_SIZE]


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def native_binary():
    binary = Path(os.environ.get("NATIVE_BINARY_PATH", str(
        Path(__file__).resolve().parents[2] / ".pio/build/native_openevse/program"))).resolve()
    assert binary.is_file(), "Build native_openevse before running these tests"
    return binary


@contextmanager
def native_instance(directory):
    """Run one isolated native firmware. Yields (HTTP base URL, log path)."""
    port = free_port()
    runtime = directory / "runtime"
    runtime.mkdir()
    environment = os.environ.copy()
    environment["EPOXY_FS_ROOT"] = str(runtime / "epoxyfsdata")
    log_path = directory / "native.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            [
                str(native_binary()),
                "--set-config", f"www_http_port={port}",
                "--set-config", "www_https_enabled=false",
            ],
            cwd=runtime,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            base = f"http://127.0.0.1:{port}"
            deadline = time.monotonic() + 45
            while True:
                assert process.poll() is None, "native firmware exited during startup"
                try:
                    if requests.get(f"{base}/config", timeout=2).status_code == 200:
                        break
                except requests.RequestException:
                    pass
                if time.monotonic() > deadline:
                    pytest.fail("native firmware HTTP listener did not start")
                time.sleep(0.1)
            yield base, log_path
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def send_raw_upload(base):
    """curl --data-binary: the image is the whole request body (application/octet-stream)."""
    return requests.post(
        f"{base}/update",
        data=firmware_image(),
        headers={"Content-Type": "application/octet-stream"},
        timeout=UPLOAD_TIMEOUT,
        allow_redirects=False,
    )


def send_multipart_upload(base):
    """Browser form / curl -F: the image is a 'firmware' part of multipart/form-data."""
    return requests.post(
        f"{base}/update",
        files={"firmware": ("firmware.bin", firmware_image(), "application/octet-stream")},
        timeout=UPLOAD_TIMEOUT,
        allow_redirects=False,
    )


def assert_upload_accepted(response):
    assert response.status_code == 200, (
        f"firmware upload rejected with HTTP {response.status_code}: {response.text[:200]!r}"
    )
    assert response.text == "OK"


def declared_update_size(log_path):
    """Size the firmware passed to Update.begin() for the upload it just ran."""
    matches = UPDATE_START.findall(log_path.read_text(errors="replace"))
    assert matches, "firmware did not log 'Update Start' for the upload"
    return int(matches[-1][1])


@pytest.mark.timeout(120)
def test_raw_body_firmware_upload_is_accepted(tmp_path):
    with native_instance(tmp_path) as (base, _):
        assert_upload_accepted(send_raw_upload(base))


@pytest.mark.timeout(120)
def test_multipart_firmware_upload_is_accepted(tmp_path):
    with native_instance(tmp_path) as (base, _):
        assert_upload_accepted(send_multipart_upload(base))


@pytest.mark.timeout(120)
def test_raw_body_declares_exact_firmware_size(tmp_path):
    with native_instance(tmp_path) as (base, log_path):
        assert_upload_accepted(send_raw_upload(base))
    assert declared_update_size(log_path) == FIRMWARE_SIZE


@pytest.mark.timeout(120)
@pytest.mark.xfail(
    strict=True,
    reason="OpenEVSE/openevse_esp32_firmware#1359: multipart uploads declare the "
           "multipart body length, not the image size. Remove this marker with the fix.",
)
def test_multipart_declares_exact_firmware_size(tmp_path):
    with native_instance(tmp_path) as (base, log_path):
        assert_upload_accepted(send_multipart_upload(base))
    assert declared_update_size(log_path) == FIRMWARE_SIZE
