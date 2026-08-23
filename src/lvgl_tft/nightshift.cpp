// src/lvgl_tft/nightshift.cpp — palette instances + active-theme selection.
// See nightshift.h. Hexes are the exact gui-nightshift/src/app.css values for the
// [data-theme="dark"] and [data-theme="light"] (the :root default) token sets.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include "nightshift.h"

// nightshift (dark) — [data-theme="dark"]
const NsPalette ns_dark = {
  .surface  = lv_color_hex(0x0C0E13),
  .surface2 = lv_color_hex(0x10141C),
  .surface3 = lv_color_hex(0x161B26),
  .text     = lv_color_hex(0xE8ECF2),
  .textdim  = lv_color_hex(0x6B7585),
  .accent   = lv_color_hex(0x3CC6BD),
  .border   = lv_color_hex(0x1C2230),
  .charging = lv_color_hex(0x3CC6BD),
  .error    = lv_color_hex(0xF06E66),
  .warning  = lv_color_hex(0xE7A948),
  .sleep    = lv_color_hex(0x7DA7C8),
  .success  = lv_color_hex(0x5DC975),
};

// light — [data-theme="light"] (also the web GUI's :root fallback)
const NsPalette ns_light = {
  .surface  = lv_color_hex(0xFFFFFF),
  .surface2 = lv_color_hex(0xEEF4F3),
  .surface3 = lv_color_hex(0xDDE7E6),
  .text     = lv_color_hex(0x13202B),
  .textdim  = lv_color_hex(0x5B6B72),
  .accent   = lv_color_hex(0x0F9B98),
  .border   = lv_color_hex(0xE4EAE9),
  .charging = lv_color_hex(0x0F9B98),
  .error    = lv_color_hex(0xD6453D),
  .warning  = lv_color_hex(0xD98A2B),
  .sleep    = lv_color_hex(0x6792B3),
  .success  = lv_color_hex(0x2EA052),
};

const NsPalette *ns_active = &ns_dark;

void ns_set_theme(bool light)
{
  ns_active = light ? &ns_light : &ns_dark;
}

#endif // ENABLE_SCREEN_LVGL_TFT
