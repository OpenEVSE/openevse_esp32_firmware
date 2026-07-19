#!/usr/bin/env bash
#
# Generate an RSA-2048 keypair for OpenEVSE signed OTA.
#
#   ota_private.pem  -> keep SECRET. Store as the GitHub Actions secret
#                       OTA_SIGNING_KEY (paste the full PEM). Never commit it.
#   ota_public.pem   -> paste into src/ota_signing.cpp (OTA_PUBLIC_KEY_PEM).
#
# Rotating the key means regenerating both, updating the embedded public key,
# and replacing the secret. Devices running a build with the old key will only
# accept images signed by the old key until they are updated (over USB, or with
# a final old-key-signed build that carries the new key).
set -euo pipefail

out_dir="${1:-.}"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "${out_dir}/ota_private.pem"
openssl rsa -in "${out_dir}/ota_private.pem" -pubout -out "${out_dir}/ota_public.pem"

echo "Wrote ${out_dir}/ota_private.pem (SECRET) and ${out_dir}/ota_public.pem (embed)."
echo
echo "Public key to embed in src/ota_signing.cpp:"
cat "${out_dir}/ota_public.pem"
