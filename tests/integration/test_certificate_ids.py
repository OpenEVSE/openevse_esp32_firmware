"""Standalone native certificate ID/storage regressions.

Build with scripts/pio run -e native_openevse -j 2, then run this file with
Python (optionally passing a unittest test name). No emulator or integration
conftest fixtures are used. All certificates and keys are generated locally.
"""

import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest

import requests


BINARY = Path(os.environ.get(
    "NATIVE_BINARY_PATH",
    Path(__file__).resolve().parents[2] / ".pio/build/native_openevse/program",
)).resolve()


class CertificateIdTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(BINARY.is_file(), "Build native_openevse first")
        # Keep generated private material in a private temporary directory.
        self.directory = tempfile.TemporaryDirectory(prefix="certificate-ids-")
        self.addCleanup(self.directory.cleanup)
        self.runtime = Path(self.directory.name)
        self.filesystem = self.runtime / "filesystem"
        self.process = None
        self.addCleanup(self.stop)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            self.port = listener.getsockname()[1]
        self.base = f"http://127.0.0.1:{self.port}"
        key = self.runtime / "dummy.key"
        key.touch(mode=0o600)
        subprocess.run(
            ["openssl", "req", "-new", "-x509", "-newkey", "ec",
             "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes",
             "-keyout", str(key), "-out", str(self.runtime / "dummy.crt"),
             "-days", "2", "-subj", "/CN=example.invalid",
             "-set_serial", "0xABCDEF1234567890"],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        self.payload = {
            "name": "dummy-certificate",
            "certificate": (self.runtime / "dummy.crt").read_text(),
            "key": key.read_text(),
        }
        self.assertLess(len(json.dumps(self.payload)), 4096)
        self.start()

    def start(self):
        environment = os.environ.copy()
        environment["EPOXY_FS_ROOT"] = str(self.filesystem)
        with (self.runtime / "native.log").open("ab") as log:
            self.process = subprocess.Popen(
                [str(BINARY), "--set-config", f"www_http_port={self.port}"],
                cwd=self.runtime, env=environment, stdout=log, stderr=subprocess.STDOUT,
            )
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll(), "Native firmware exited during startup")
            try:
                if requests.get(f"{self.base}/config", timeout=2).status_code == 200:
                    return
            except requests.RequestException:
                pass
            time.sleep(0.1)
        self.fail("Native firmware did not become ready")

    def stop(self):
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)

    def restart(self):
        self.stop()
        self.start()

    def upload(self, certificate_id=None):
        payload = dict(self.payload)
        if certificate_id is not None:
            payload["id"] = certificate_id
        response = requests.post(f"{self.base}/certificates", json=payload, timeout=10)
        self.assertEqual(response.status_code, 200)
        return response.json()["id"]

    def test_full_width_uppercase_ids(self):
        expected_ids = ["ABCDEF1234567890", "0", "123456789ABCDEF",
                        "8000000000000000", "FEDCBA9876543210", "FFFFFFFFFFFFFFFF"]
        for index, expected in enumerate(expected_ids):
            with self.subTest(certificate_id=expected):
                actual = self.upload(None if index == 0 else expected.lower())
                self.assertEqual(actual, expected)
                record = self.filesystem / "certificates" / f"{expected}.json"
                self.assertTrue(record.is_file())
                self.assertEqual(json.loads(record.read_text())["id"], expected)
        self.restart()
        response = requests.get(f"{self.base}/certificates", timeout=10)
        self.assertEqual(response.status_code, 200)
        self.assertEqual({item["id"] for item in response.json()}, set(expected_ids))

    def check_legacy_delete(self, keep_canonical):
        certificate_id = self.upload("ABCDEF1234567890")
        self.stop()
        canonical = self.filesystem / "certificates" / f"{certificate_id}.json"
        legacy = canonical.with_name(f"{certificate_id.lower()}.json")
        self.assertNotEqual(canonical, legacy)
        if keep_canonical:
            legacy.write_bytes(canonical.read_bytes())
        else:
            canonical.rename(legacy)
        self.start()
        fetched = requests.get(f"{self.base}/certificates/{certificate_id.lower()}", timeout=10)
        self.assertEqual(fetched.status_code, 200)
        self.assertEqual(fetched.json()["id"], "ABCDEF1234567890")
        deleted = requests.delete(f"{self.base}/certificates/{certificate_id}", timeout=10)
        self.assertEqual(deleted.status_code, 200)
        self.restart()
        listed = requests.get(f"{self.base}/certificates", timeout=10)
        self.assertEqual(listed.status_code, 200)
        self.assertEqual([item["id"] for item in listed.json()], [])
        self.assertFalse(canonical.exists())
        self.assertFalse(legacy.exists())

    def test_delete_legacy_only(self):
        self.check_legacy_delete(keep_canonical=False)

    def test_delete_both_filename_variants(self):
        self.check_legacy_delete(keep_canonical=True)

    def test_maximum_id_get(self):
        certificate_id = "FFFFFFFFFFFFFFFF"
        self.assertEqual(self.upload(certificate_id), certificate_id)
        fetched = requests.get(f"{self.base}/certificates/{certificate_id.lower()}", timeout=10)
        self.assertEqual(fetched.status_code, 200)
        self.assertTrue(isinstance(fetched.json(), dict), "An ID route must return one certificate")
        self.assertEqual(fetched.json()["id"], certificate_id)

    def test_maximum_id_delete(self):
        certificate_id = "FFFFFFFFFFFFFFFF"
        self.assertEqual(self.upload(certificate_id), certificate_id)
        deleted = requests.delete(f"{self.base}/certificates/{certificate_id}", timeout=10)
        self.assertEqual(deleted.status_code, 200)
        self.restart()
        fetched = requests.get(f"{self.base}/certificates/{certificate_id}", timeout=10)
        self.assertEqual(fetched.status_code, 404)
        listed = requests.get(f"{self.base}/certificates", timeout=10)
        self.assertEqual(listed.status_code, 200)
        self.assertEqual([item["id"] for item in listed.json()], [])

    def test_maximum_id_post_is_not_collection_post(self):
        response = requests.post(f"{self.base}/certificates/FFFFFFFFFFFFFFFF",
                                 json=self.payload, timeout=10)
        self.assertEqual(response.status_code, 405)
        listed = requests.get(f"{self.base}/certificates", timeout=10)
        self.assertEqual(listed.status_code, 200)
        self.assertEqual([item["id"] for item in listed.json()], [])

    def test_missing_maximum_id_is_not_collection_get(self):
        fetched = requests.get(f"{self.base}/certificates/FFFFFFFFFFFFFFFF", timeout=10)
        self.assertEqual(fetched.status_code, 404)
        listed = requests.get(f"{self.base}/certificates", timeout=10)
        self.assertEqual(listed.status_code, 200)
        self.assertEqual([item["id"] for item in listed.json()], [])
        deleted = requests.delete(f"{self.base}/certificates", timeout=10)
        self.assertEqual(deleted.status_code, 405)


if __name__ == "__main__":
    unittest.main(verbosity=2)
