# signed_ota branch changes

Addresses the coordinated-disclosure report "Chained Reflected XSS, Insecure
Default Configuration, and Unsigned Arbitrary-URL Firmware Update" (F1 insecure
default auth, F2 unsigned arbitrary-URL OTA, F3 reflected XSS in `/r`).

Branched from `master`.

> **Status (after on-device testing):** F3 (XSS) and F2a (URL allowlist) are
> done and sound. F2b **signed OTA is not production-ready** — the on-device
> verifier is proven working, but delivering a correctly-signed image hits a
> core-library flash-erase bug on the exact-size streaming path. Keep signing
> **disabled** (no `OTA_SIGNING_KEY` secret) until this is resolved. See the
> Testing section for detail.

## Decisions (given the "no forced password" constraint)

Forcing a web password was explicitly rejected (support burden when users
forget). So **F1 is intentionally left as-is** — the device stays open on the
LAN by default. The report's real teeth are the *chain*, so this branch breaks
the chain instead of the default:

- **F3** (reflected XSS) is fixed outright — it's a clear bug and removes the
  one-click path against password-configured devices.
- **F2** (unsigned arbitrary-URL OTA) gets two independent controls: an
  HTTPS-only GitHub allowlist on the URL-fetch path, and **signed firmware
  images** verified on-device. Signing is the one control that closes the
  unauthenticated-reflash path *even on a password-less, open-by-default
  device*, because a LAN attacker cannot produce a validly-signed image.

Residual, by design: with no password and no signing, a LAN host can still
upload an image via the authenticated-but-open file-upload path. Signed OTA
closes that on the boards that support it (see core-3.x note); the 4MB boards
retain the LAN-trust posture you're choosing.

## F3 — Reflected XSS fix (`src/web_server.cpp`)

- New `htmlEscape()` helper; the `rapi` parameter (and the RAPI response string)
  are escaped in both the success and error branches of `handleRapi()`, so
  `/r?rapi=<script>…` can no longer inject into the `text/html` response.
- Baseline security headers added to **all** responses in `requestPreProcess()`:
  `X-Content-Type-Options: nosniff` and `X-Frame-Options: SAMEORIGIN`. A global
  `Content-Security-Policy` is deliberately *not* set (it can break the bundled
  SPA).
- The `/r` console gets its own strict CSP
  (`default-src 'none'; form-action 'self'; base-uri 'none'`) as defence in
  depth; its cosmetic inline focus-script was removed so the policy can forbid
  all scripting.

## F2a — URL-fetch allowlist (`src/http_update.cpp` / `.h`)

`http_update_url_allowed()` requires `https://` and a host of `github.com` or
`*.githubusercontent.com` (GitHub redirects release downloads to the
`githubusercontent` CDN). It is enforced at the top of `http_update_from_url()`,
which runs on the initial fetch **and** on every 30x redirect target, so a
device can only pull firmware from a trusted origin. Userinfo (`user@host`) is
stripped to defeat `github.com@evil.example` tricks. Rejected fetches return
`HTTP_UPDATE_ERROR_URL_NOT_ALLOWED`.

The **file-upload** path (choosing a `.bin` in the GUI) is unchanged — this is
how local/dev builds are flashed, and it satisfies "updates come from OpenEVSE
GitHub" (the browser downloads from GitHub, then uploads).

The allowlist is one editable function; relax or make it configurable upstream
if self-hosted update URLs need to be supported.

## F2b — Signed OTA (`src/ota_signing.*`, `scripts/`, CI)

Uses the Arduino-ESP32 **core-3.x** `Update` library's built-in verifier
(`UpdaterRSAVerifier` + `Update.installSignature()`), so the on-device code is
small.

- `src/ota_signing.cpp/.h` embeds the RSA-2048 **public** key and, when built
  with `-DREQUIRE_SIGNED_OTA`, installs the verifier. `http_update_start()` —
  the single `Update.begin()` choke point for **both** the URL and file-upload
  paths — calls it just before `begin()`, and fails closed if the verifier
  can't be installed or the Content-Length is unknown (needed to locate the
  signature trailer).
- Image format (matched exactly to the core verifier): `firmware || signature ||
  zero-pad`, a fixed **512-byte** trailer. The signature is **RSA-2048 PSS**
  over `SHA-256(firmware)`, MGF1-SHA256, **salt length 222** (`key_len − 32 −
  2`), 256 bytes, zero-padded to 512. `begin(total)` is given
  `len(firmware)+512` (the served Content-Length).
- `scripts/sign_firmware.py` produces exactly this (verified: a signed test
  image validates against the public key with device-identical PSS params, and
  a one-byte tamper is rejected). `scripts/generate_ota_key.sh` regenerates the
  keypair for rotation.

### How "on in CI, off locally" works

Enforcement is decided by the firmware **running on the receiving device**, not
by how an incoming image was built:

- Local/dev builds have no flags → accept unsigned images (flash over USB as
  usual, or OTA a dev build to a dev device).
- CI release builds (once enabled) compile with `-DUPDATE_SIGN
  -DREQUIRE_SIGNED_OTA` and the image is signed → a device running such a build
  rejects any unsigned OTA. To put an unsigned local build on such a device, use
  USB/serial.

### CI (dormant until you opt in)

`.github/workflows/build.yaml` signs and enforces **only** when the
`OTA_SIGNING_KEY` secret is present *and* the env is in `SIGNED_OTA_ENVS`.
Until the secret is added, builds are byte-for-byte unchanged (unsigned) — so
merging this does not alter the fleet on its own.

### core-3.x limitation

The verifier API exists only in the **core-3.x / pioarduino / IDF5** core (the
16MB+ boards, e.g. `openevse_wifi_v1_16mb`). The older-core 4MB envs
(`openevse_wifi_v1`, …) have no `Updater_Signing.h` and **cannot** verify
signatures via this mechanism; `ota_signing.cpp` `#error`s if the flag is set on
such an env, and `SIGNED_OTA_ENVS` lists only signed-capable envs. 4MB boards
rely on the URL allowlist + F3 fix.

## Operator action items (to actually turn signing on)

1. The public key is already embedded (`src/ota_signing.cpp`). The matching
   **private** key was generated during this work and is **not** in the repo
   (`*.pem` is gitignored). Store its PEM as the GitHub Actions secret
   `OTA_SIGNING_KEY`, or regenerate with `scripts/generate_ota_key.sh` and
   replace the embedded public key.
2. Extend `SIGNED_OTA_ENVS` as more core-3.x boards are added.
3. **Rollout is a one-way door:** once a device runs a signed-required build,
   all future OTAs must be signed. If the key is lost, recovery is USB-only.
   Consider a phased rollout (ship *signed* images before flipping
   `REQUIRE_SIGNED_OTA`), and keep the private key backed up and access-limited.

## Testing

- Builds (16MB, core-3.x): unsigned default **and** `-DUPDATE_SIGN
  -DREQUIRE_SIGNED_OTA` both clean.
- Build (4MB, old core): unsigned default clean; signed flags correctly
  `#error` out (guard verified).
- Signing round-trip verified off-device: `sign_firmware.py` output validates
  against the public key with the device's exact PSS parameters (salt 222,
  256-byte sig, 512-byte trailer); a tampered image is rejected.
- F3 and the URL allowlist are on-device testable (unauthenticated `/r` probe;
  a non-GitHub `POST /update` URL should return the not-allowed error).

### On-device signed-OTA test (openevse-c620, 16MB) — verifier PASS, delivery BLOCKED

An enforcing build (`-DUPDATE_SIGN -DREQUIRE_SIGNED_OTA`) was flashed to c620
and images were pushed over the web `/update` file-upload path:

- **Verifier works.** An **unsigned** image is rejected with
  `Update failed: 14 (Signature Verification Failed)` — the on-device mbedtls
  RSA-PSS verifier ran and correctly rejected it. This validates the public
  key, the PSS parameters, and the `sign_firmware.py` trailer format against
  real silicon.
- **Delivery is blocked by a core-library flash-erase bug.** To verify a
  *correctly-signed* image the exact image size must be given to
  `Update.begin()` (so the 512-byte signature trailer can be located). With the
  exact size, the core Update library flushes the final partial sector *during*
  `write()` (Updater.cpp:791, `_bufferLen == remaining()`) instead of at
  `end()`, and that erase fails with `Update failed: 2 (Flash Erase Failed)` in
  the streaming callback context. Without the exact size the erase succeeds but
  the signature hash covers the wrong byte range, so a signed image can't be
  verified that way either.

**Status: signed OTA is NOT production-ready.** The verifier is proven, but
there is no working end-to-end delivery of a signed image on this core via the
OpenEVSE streaming update path. The `?size` file-upload hint that exposed this
was reverted. Resolving it needs one of:

1. Root-cause the exact-size final-sector erase (possibly a flash/cache-context
   issue with erasing inside the network callback) and fix delivery so
   `end()`-time flushing is used, or
2. Verify the signature in OpenEVSE code instead of via the core
   `installSignature()` API — hash incrementally over the streamed firmware,
   hold back the last 512 bytes, and RSA-PSS-verify with mbedtls at `end()` —
   avoiding the exact-size `begin()` requirement entirely.

Until then, keep signing **disabled** (no `OTA_SIGNING_KEY` secret). F3 and the
F2 URL allowlist are unaffected and stand on their own.

Note: because an enforcing build rejects every image it can't verify, c620 was
left on the enforcing test build and recovered by **USB flash** of a clean
non-enforcing image (there is no working signed-OTA path to recover it over the
network — the same delivery bug).
