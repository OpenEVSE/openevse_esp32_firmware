#include "ota_signing.h"

#if defined(REQUIRE_SIGNED_OTA)

#if !defined(UPDATE_SIGN)
#error "REQUIRE_SIGNED_OTA requires the Update library to be built with -DUPDATE_SIGN"
#endif

#include <Arduino.h>
#include <Update.h>

// The signed-update verifier API ships only in the core-3.x (pioarduino / IDF5)
// Arduino-ESP32 core used by the 16MB+ boards. Older-core (4MB) envs have no
// Updater_Signing.h, so enforce signed OTA only on core-3.x environments.
#if !defined(__has_include) || !__has_include("Updater_Signing.h")
#error "REQUIRE_SIGNED_OTA is only supported on core-3.x (pioarduino/IDF5) build environments"
#endif

#include "Updater_Signing.h"

// OpenEVSE OTA image-signing public key (RSA-2048).
//
// The matching PRIVATE key is held only in CI (GitHub Actions secret
// OTA_SIGNING_KEY) and is used by scripts/sign_firmware.py to sign release
// images. Rotating the key means replacing this block and the secret together.
static const char OTA_PUBLIC_KEY_PEM[] =
  "-----BEGIN PUBLIC KEY-----\n"
  "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAk4Bl0WXUs6x98Sph/OGe\n"
  "i5ezB+nemFLcLHrcDc3zXsefZ3bek8YeXck+ggswYLnRR8LFwStffiqm0zscyL2U\n"
  "11Vwc/sRl5qBAqY5tFi0zhhHQXkmx3vA2nNQiXIYXD2YfIKO/AtAN5LQtrp0r27t\n"
  "Dqe3XujX9YlkijMpIyb+P+e2VOl//ZSK59i6mjbDtrdXp8dYnsSR5+wmZSaONH4N\n"
  "mx1CvrhdU/LCR8jqbCCbrNEU6E+oQ0beVO5znEVYbft8NYWZHItAkIahRbpk5KeT\n"
  "NNsb6QqyGiUVsuDW5iqCZ2K3w6mSC5GZ7nE8ZvvAOwuScf7jeWZr/kCEBd8s86yf\n"
  "eQIDAQAB\n"
  "-----END PUBLIC KEY-----\n";

bool ota_install_signature()
{
  // Constructed once and reused; UpdateClass stores the pointer and calls it at
  // end(). mbedtls PEM parsing needs the length to include the trailing NUL, so
  // pass sizeof (not strlen).
  static UpdaterRSAVerifier verifier(
    reinterpret_cast<const uint8_t *>(OTA_PUBLIC_KEY_PEM),
    sizeof(OTA_PUBLIC_KEY_PEM),
    HASH_SHA256);

  return Update.installSignature(&verifier);
}

bool ota_signing_required()
{
  return true;
}

#else // !REQUIRE_SIGNED_OTA

bool ota_install_signature()
{
  return true; // signing not enforced in this build
}

bool ota_signing_required()
{
  return false;
}

#endif // REQUIRE_SIGNED_OTA
