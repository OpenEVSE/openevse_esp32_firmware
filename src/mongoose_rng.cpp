// Real RNG for Mongoose's mbedTLS TLS layer.
//
// Mongoose declares mg_ssl_if_mbed_random() `extern` and uses it for
// mbedtls_ssl_conf_rng(), but only *defines* a dummy rand()-based version under
// -D MG_SSL_MBED_DUMMY_RANDOM -- which its own source flags as "a bad idea ... in
// production" (a predictable PRNG makes TLS session keys recoverable). We drop
// that build flag and provide the real implementation here, backed by the ESP32
// hardware RNG via esp_fill_random().
//
// Entropy quality of esp_random()/esp_fill_random():
//  * ESP32 / ESP32-C3: true-random whenever Wi-Fi (or BT) is enabled, which the
//    gateway always is by the time TLS runs.
//  * ESP32-P4: no on-die radio (Wi-Fi is on the C6 over ESP-Hosted), so the
//    Wi-Fi-feeds-the-RNG guarantee does not apply. hardware_setup() enables the
//    internal SAR-ADC noise entropy source (bootloader_random_enable) at boot so
//    the HWRNG is seeded from on-chip noise instead.
#include <stddef.h>
#include <esp_random.h>

extern "C" int mg_ssl_if_mbed_random(void *ctx, unsigned char *buf, size_t len)
{
  (void) ctx;
  esp_fill_random(buf, len);
  return 0;
}
