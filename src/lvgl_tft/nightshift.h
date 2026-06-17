// src/lvgl_tft/nightshift.h — the nightshift palette, exact hexes from the source
// of truth gui-nightshift/src/app.css (dark theme), shared with the P4 EEZ UI so
// the stock TFT, the P4 panel and the web GUI all match. See
// docs/superpowers/specs/2026-05-31-p4-eez-nightshift-theme.md.
#ifndef __NIGHTSHIFT_H
#define __NIGHTSHIFT_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <lvgl.h>

#define NS_SURFACE   lv_color_hex(0x0C0E13) // screen base
#define NS_SURFACE2  lv_color_hex(0x10141C)
#define NS_SURFACE3  lv_color_hex(0x161B26) // cards / tiles
#define NS_TEXT      lv_color_hex(0xE8ECF2)
#define NS_TEXTDIM   lv_color_hex(0x6B7585)
#define NS_ACCENT    lv_color_hex(0x3CC6BD) // brand teal
#define NS_BORDER    lv_color_hex(0x1C2230) // dividers / ring track
#define NS_CHARGING  lv_color_hex(0x3CC6BD) // == accent (web GUI power ring)
#define NS_ERROR     lv_color_hex(0xF06E66)
#define NS_WARNING   lv_color_hex(0xE7A948)
#define NS_SLEEP     lv_color_hex(0x7DA7C8)
#define NS_SUCCESS   lv_color_hex(0x5DC975)

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __NIGHTSHIFT_H
