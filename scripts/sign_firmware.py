#!/usr/bin/env python3
"""
Sign an OpenEVSE firmware image for signed OTA.

Produces the exact trailer format the ESP32 Arduino `Update` library verifies
when built with -DUPDATE_SIGN and Update.installSignature(UpdaterRSAVerifier):

    output = firmware || signature(key_len bytes) || zero-pad to 512

The device hashes the firmware bytes (everything before the 512-byte trailer)
with SHA-256 and verifies the leading `key_len` bytes of the trailer as an
RSA-PSS signature (MGF1-SHA256, salt length = key_len - 32 - 2). For RSA-2048
that is a 256-byte signature, salt length 222, zero-padded to 512.

The receiving device must be told the total size = len(firmware) + 512
(Content-Length of the served file), which OTA does automatically.

Usage:
    python sign_firmware.py --key ota_private.pem firmware.bin firmware.signed.bin
    # key may also come from the env var named by --key-env (e.g. CI secret)
    python sign_firmware.py --key-env OTA_SIGNING_KEY firmware.bin firmware.signed.bin

Requires: cryptography  (pip install cryptography)
"""
import argparse
import os
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

TRAILER_SIZE = 512  # fixed padded trailer the Update library reads


def load_private_key(pem_bytes):
    key = serialization.load_pem_private_key(pem_bytes, password=None)
    if not isinstance(key, rsa.RSAPrivateKey):
        sys.exit("error: signing key is not an RSA private key")
    return key


def sign(firmware, key):
    key_len = key.key_size // 8  # 256 for RSA-2048
    if key_len > TRAILER_SIZE:
        sys.exit(f"error: RSA key length {key_len} exceeds trailer size {TRAILER_SIZE}")

    # Match mbedtls verifier exactly: expected_salt_len = key_len - hash_size - 2.
    salt_len = key_len - hashes.SHA256.digest_size - 2

    signature = key.sign(
        firmware,
        padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=salt_len),
        hashes.SHA256(),
    )
    if len(signature) != key_len:
        sys.exit(f"error: unexpected signature length {len(signature)} != {key_len}")

    trailer = signature + b"\x00" * (TRAILER_SIZE - len(signature))
    return firmware + trailer


def main():
    ap = argparse.ArgumentParser(description="Sign an OpenEVSE firmware image for signed OTA")
    ap.add_argument("input", help="unsigned firmware .bin")
    ap.add_argument("output", help="signed firmware .bin to write")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--key", help="path to RSA private key PEM")
    src.add_argument("--key-env", help="env var holding the RSA private key PEM (e.g. a CI secret)")
    args = ap.parse_args()

    if args.key:
        with open(args.key, "rb") as f:
            pem = f.read()
    else:
        val = os.environ.get(args.key_env)
        if not val:
            sys.exit(f"error: env var {args.key_env} is empty or unset")
        pem = val.encode() if isinstance(val, str) else val

    key = load_private_key(pem)

    with open(args.input, "rb") as f:
        firmware = f.read()

    signed = sign(firmware, key)

    with open(args.output, "wb") as f:
        f.write(signed)

    print(
        f"signed {args.input} ({len(firmware)} bytes) -> {args.output} "
        f"({len(signed)} bytes, +{TRAILER_SIZE}-byte RSA-{key.key_size} PSS trailer)"
    )


if __name__ == "__main__":
    main()
