// src/lvgl_tft/lv_conf.h — LVGL 8.3 config for the stock OpenEVSE ILI9488 TFT
// (ESP32-WROOM, no PSRAM). Selected via -I src/lvgl_tft on env openevse_wifi_tft_v1.
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// 16-bit colour. TFT_eSPI pushes via pushPixels; LV_COLOR_16_SWAP matches its
// byte order so the buffer goes out verbatim. If colours look swapped on
// hardware, flip this to 0 (and add tft.setSwapBytes(true) in lvgl_panel.cpp).
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

// LVGL widget/render scratch pool. Static (BSS) array of this size — the partial
// draw buffer is separate and lives in internal DRAM (lvgl_panel.cpp). No PSRAM
// on this board, and this branch's static footprint is large, so keep it modest:
// 32 KB is ample for one static screen (an arc + ~12 labels) and leaves DRAM for
// the WiFi/TLS/Mongoose stack + the ~30 KB draw buffer.
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)

// Tick from Arduino millis().
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF 130

// No on-screen instrumentation in production (those were spike overlays).
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_USE_LOG 0
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Fonts used by charge_screen.cpp.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// lv_label_set_text_fmt()/lv_snprintf need this for %f.
#define LV_SPRINTF_USE_FLOAT 1

// Widgets used: label, arc.
#define LV_USE_ARC 1
#define LV_USE_LABEL 1

#endif // LV_CONF_H
