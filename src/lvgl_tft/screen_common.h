// Shared helpers for the LVGL TFT screens (charge + standby).
#ifndef __SCREEN_COMMON_H
#define __SCREEN_COMMON_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>
#include <lvgl.h>

// Map an OPENEVSE_STATE_* value to a status word + accent colour (nightshift).
const char *state_word(uint8_t evse_state, lv_color_t *colour);

// RSSI (dBm) -> signal %, the usual piecewise mapping.
int wifi_percent(int rssi);

// Format a temperature (input always °C) into buf as e.g. "22.3C  " or, when
// fahrenheit is set, "72.1F  " (converting first). Trailing two spaces match
// the top-strip layout. Returns the number of characters written (like snprintf).
int fmt_temp(char *buf, size_t n, float temp_c, bool fahrenheit);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __SCREEN_COMMON_H
