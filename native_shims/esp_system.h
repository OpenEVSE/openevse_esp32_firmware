#pragma once

// Minimal host-build shim for ESP-IDF chip info API used by ESPAL's ESP32 HAL.
// Used only for PlatformIO `native` builds.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CHIP_ESP32 = 1,
  CHIP_ESP32S2 = 2,
  CHIP_ESP32S3 = 3,
  CHIP_ESP32C3 = 4,
  CHIP_ESP32H2 = 5,
} esp_chip_model_t;

enum {
  CHIP_FEATURE_WIFI_BGN = 1 << 0,
  CHIP_FEATURE_BLE = 1 << 1,
  CHIP_FEATURE_BT = 1 << 2,
  CHIP_FEATURE_EMB_FLASH = 1 << 3,
};

typedef struct {
  esp_chip_model_t model;
  uint32_t features;
  uint8_t cores;
  uint16_t revision;
} esp_chip_info_t;

static inline void esp_chip_info(esp_chip_info_t* out_info) {
  if (!out_info) return;
  out_info->model = CHIP_ESP32;
  out_info->features = CHIP_FEATURE_WIFI_BGN;
  out_info->cores = 2;
  out_info->revision = 0;
}

#ifdef __cplusplus
}
#endif
