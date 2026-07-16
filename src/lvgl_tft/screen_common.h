// Shared helpers for the LVGL TFT screens (charge + standby).
#ifndef __SCREEN_COMMON_H
#define __SCREEN_COMMON_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>
#include <lvgl.h>

// Max rendered width (px) for the centre state word at the large font before we
// step down a size. Sized to the ring's usable inner width (200px ring - 14px
// band each side) so long words ("NOT CONNECTED") never wrap onto the band.
// The charge and standby rings share this geometry.
#define STATE_WORD_FIT_W 172

// Map an OPENEVSE_STATE_* value to a status word + accent colour (nightshift).
const char *state_word(uint8_t evse_state, lv_color_t *colour);

// RSSI (dBm) -> signal %, the usual piecewise mapping.
int wifi_percent(int rssi);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __SCREEN_COMMON_H
