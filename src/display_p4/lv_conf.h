// src/display_p4/lv_conf.h — LVGL 8.3 config for the ESP32-P4 ST7701 MIPI-DSI panel.
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// RGB565. DSI panel takes native byte order — NO swap (unlike the SPI/TFT spike).
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// LVGL widget/render scratch pool (internal RAM). Big draw buffers live in PSRAM,
// allocated separately in display_p4.cpp.
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)

// Tick from Arduino millis().
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF 130

// On-screen FPS/CPU + memory instrumentation (useful for the bring-up).
#define LV_USE_PERF_MONITOR 1
#define LV_USE_MEM_MONITOR 1

#define LV_USE_LOG 0
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Fonts used by lv_demo_widgets.
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// D1 bring-up content: the LVGL widgets demo.
#define LV_USE_DEMO_WIDGETS 1

#endif // LV_CONF_H
