#ifndef OTA_SIGNING_H
#define OTA_SIGNING_H

// Install the compiled-in RSA public key as the signature verifier on the
// global `Update` object, immediately before `Update.begin()`.
//
// When the firmware is built with -DREQUIRE_SIGNED_OTA (which also requires the
// Update library to be built with -DUPDATE_SIGN), every OTA image must carry a
// valid RSA-2048/SHA-256 (PSS) signature over the firmware, appended as a
// fixed 512-byte trailer, or the update is rejected at end(). See
// scripts/sign_firmware.py for how release images are signed.
//
// In a normal (unsigned) build this is a no-op that returns true, so the update
// path is unchanged.
//
// Returns false only when signing is required but the verifier could not be
// installed; the caller must abort the update in that case (fail closed).
bool ota_install_signature();

// True when this build enforces signed OTA (compile-time).
bool ota_signing_required();

#endif // OTA_SIGNING_H
