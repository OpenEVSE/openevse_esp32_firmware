#include "web_auth_secret.h"
#include "app_config.h"
#include <esp_random.h>

// Generate a 64-character lowercase hex string backed by 32 bytes of hardware
// entropy via esp_fill_random(). On ESP32/C3, esp_fill_random() draws from
// the Wi-Fi-seeded HWRNG; on P4 it draws from the SAR-ADC noise source
// enabled at boot by hardware_setup().
static String randomHex32()
{
  uint8_t buf[32];
  esp_fill_random(buf, sizeof(buf));
  static const char *H = "0123456789abcdef";
  String s;
  s.reserve(64);
  for (size_t i = 0; i < sizeof(buf); i++) {
    s += H[buf[i] >> 4];
    s += H[buf[i] & 0xf];
  }
  return s;
}

String web_auth_get_secret()
{
  // Read-only: returns whatever is currently in server_secret (may be empty).
  return server_secret;
}

void web_auth_ensure_secret()
{
  if (server_secret.length() == 0) {
    web_auth_rotate_secret();
  }
}

void web_auth_rotate_secret()
{
  server_secret = randomHex32();
  // Persist via config_user_commit() — not config_commit(), which also sets
  // the factory_write_lock flag.
  config_user_commit();
}
