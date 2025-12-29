#pragma once

// Minimal host-build shim for a tiny subset of ESP-IDF esp_wifi.h used by the firmware.
// Used only for PlatformIO `native` builds.

#ifdef __cplusplus
extern "C" {
#endif

static inline int esp_wifi_set_country_code(const char* /*country*/, bool /*ieee80211d_enabled*/) {
  return 0;
}

#ifdef __cplusplus
}
#endif
