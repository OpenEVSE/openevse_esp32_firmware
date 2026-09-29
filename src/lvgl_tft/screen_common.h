// Shared helpers for the LVGL TFT screens (charge + standby).
#ifndef __SCREEN_COMMON_H
#define __SCREEN_COMMON_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>
#include <lvgl.h>

// Shared ring geometry. The charge and standby screens draw the same instrument
// at the same place and size, so switching between them doesn't move it --
// standby should read as this screen dimmed, not as a different one.
#define RING_SIZE  188          // outer diameter
#define RING_LEFT   24          // LV_ALIGN_LEFT_MID x offset
#define RING_Y       2          // LV_ALIGN_LEFT_MID y offset
#define RING_BAND   14          // band width

// Max rendered width (px) for the centre state word at the large font before we
// step down a size. Sized to the ring's usable inner width (RING_SIZE less
// RING_BAND each side) so long words ("NOT CONNECTED") never wrap onto the band.
#define STATE_WORD_FIT_W 150

// Shared right-hand stat tile geometry, so the two screens' columns line up.
#define TILE_X  248
#define TILE_W  224
#define TILE_H   80
#define TILE_Y0  54
#define TILE_Y1 140
#define TILE_Y2 226

// The standby screen has no second top-strip line (its address sits in a footer
// instead), so its tiles rise by that line's height. That is also exactly what
// frees the band the footer needs at the bottom.
#define TILE_LIFT 14

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
