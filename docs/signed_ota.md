# OTA update hardening (URL allowlist + signed images)

Firmware-update controls from the "unsigned arbitrary-URL OTA" finding (F2 of
the chained-RCE advisory). The reflected-XSS fix (F3) is handled separately in
#1166 and is not part of this change.

> **Status:** the URL allowlist (F2a) is complete. Signed OTA (F2b) is
> infrastructure-only and **shipped disabled** — the on-device verifier is
> proven, but there is no working delivery path for a signed image yet (see
> "Signed OTA status" below). Do **not** configure the `OTA_SIGNING_KEY` secret
> until that is resolved; without it, builds are unchanged and unsigned.

## F2a — URL-fetch allowlist

`POST /update {"url": ...}` fetches a URL and flashes it. `handleUpdateFileFetch`
→ `http_update_from_url()` previously accepted any URL. Now every fetch is gated
by `http_update_url_allowed()` (a thin wrapper over the pure, unit-tested
`ota_url_host_allowed()` in `src/ota_url_allow.cpp`):

- scheme must be `https` (case-insensitive);
- host must be `github.com` (or a subdomain) or `*.githubusercontent.com`
  (GitHub redirects release downloads to that CDN).

Enforced on the initial fetch **and** on every 30x redirect target (the function
re-enters itself for `Location`), so firmware can only come from a trusted
origin regardless of the redirect chain.

Parsing follows the URL spec so it can't be fooled by userinfo/port tricks:
the authority ends at the first `/` or `?` (not `:`), userinfo is stripped at the
**last** `@`, then the port is stripped at the host's `:`. So
`https://github.com:x@evil.example/…` resolves to host `evil.example` and is
rejected — the same way the underlying HTTP client resolves it. Vectors are
covered by `test/test_ota_url_allow/` (run via `pio test -e native_test`).

The **file-upload** path (choosing a `.bin` in the GUI) is unchanged and is how
local/dev builds are flashed; it satisfies "updates come from OpenEVSE GitHub"
(the browser downloads from GitHub, then uploads). The allowlist is one editable
function; relax it upstream if self-hosted update URLs must be supported.

## F2b — Signed OTA (infrastructure, disabled)

Uses the Arduino-ESP32 **core-3.x** `Update` library's built-in verifier
(`UpdaterRSAVerifier` + `Update.installSignature()`).

- `src/ota_signing.cpp/.h` embeds the RSA-2048 **public** key and, under
  `-DREQUIRE_SIGNED_OTA`, installs the verifier. `http_update_start()` — the
  single `Update.begin()` choke point for both the URL and file-upload paths —
  installs it just before `begin()` and fails closed if the verifier can't be
  installed or the Content-Length is unknown.
- Image format (matched to the core verifier): `firmware || signature ||
  zero-pad`, a fixed **512-byte** trailer. The signature is **RSA-2048 PSS**
  over `SHA-256(firmware)`, MGF1-SHA256, **salt length 222** (`key_len − 32 −
  2`), 256 bytes zero-padded to 512.
- `scripts/sign_firmware.py` produces exactly this (verified: a signed test
  image validates against the public key with device-identical PSS params, and a
  one-byte tamper is rejected). `scripts/generate_ota_key.sh` regenerates the
  keypair for rotation.

### "On in CI, off locally"

Enforcement is decided by the firmware **running on the receiving device**, not
by how an incoming image was built. Local/dev builds have no flags and accept
unsigned images (flash over USB). CI release builds (once enabled) compile with
`-DUPDATE_SIGN -DREQUIRE_SIGNED_OTA` and sign the image, so a device running such
a build rejects unsigned OTA.

`.github/workflows/build.yaml` signs and enforces **only** when the
`OTA_SIGNING_KEY` secret is present *and* the env is in `SIGNED_OTA_ENVS`. Until
the secret is added, builds are byte-for-byte unchanged.

### core-3.x limitation

The verifier API exists only in the **core-3.x / pioarduino / IDF5** core (the
16MB+ boards, e.g. `openevse_wifi_v1_16mb`). Older-core 4MB envs have no
`Updater_Signing.h`; `ota_signing.cpp` `#error`s if the flag is set there, and
`SIGNED_OTA_ENVS` lists only signed-capable envs.

### Signed OTA status — verifier PASS, delivery BLOCKED

On-device test (openevse-c620, 16MB) with an enforcing build:

- **Verifier works.** An unsigned image is rejected with `Update failed: 14
  (Signature Verification Failed)` — the mbedtls RSA-PSS verifier ran and
  correctly rejected it, validating the key, PSS params, and trailer format on
  real silicon.
- **Delivery is blocked.** Verifying a *correctly-signed* image needs the exact
  image size at `Update.begin()`. With the exact size, the core Update library
  flushes the final partial sector *during* `write()` (Updater.cpp:791) instead
  of at `end()`, and that erase fails with `Update failed: 2 (Flash Erase
  Failed)`. Without the exact size, the erase succeeds but the signature hash
  covers the wrong byte range. Either way a signed image can't complete.

  Separately (and independent of the erase bug), the file-upload path passes
  `request->contentLength()` — the whole multipart envelope, not the image size
  — to `begin()`, so it structurally can't feed the verifier `len(firmware)+512`
  either. Both need addressing before signed OTA works end-to-end.

Two ways forward for whoever picks this up:

1. Root-cause the exact-size final-sector erase and make delivery flush at
   `end()`; and parse the multipart part length for the file-upload size.
2. Verify the signature in OpenEVSE code (hash incrementally over the streamed
   firmware, hold back the last 512 bytes, RSA-PSS-verify with mbedtls at
   `end()`), avoiding the exact-size `begin()` requirement entirely.

Until then, keep signing disabled.

## Turning signing on later (operator checklist)

1. The public key is embedded in `src/ota_signing.cpp`; the matching **private**
   key is not in the repo (`*.pem` is gitignored). Store its PEM as the GitHub
   Actions secret `OTA_SIGNING_KEY`, or regenerate with
   `scripts/generate_ota_key.sh` and replace the embedded public key.
2. Extend `SIGNED_OTA_ENVS` as more core-3.x boards are added.
3. **Rollout is a one-way door:** once a device runs a signed-required build, all
   future OTAs must be signed (USB-only recovery if the key is lost). Consider
   shipping *signed* images before flipping `REQUIRE_SIGNED_OTA`, and back up the
   private key.
