// src/lvgl_tft/nightshift.cpp — palette instances + active-theme selection.
// See nightshift.h for why these are hue-matched to gui-nightshift/src/app.css
// rather than hex-identical to it. The trailing comment on each retuned line is
// the app.css value it derives from, so the drift stays auditable.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include "nightshift.h"

// nightshift (dark) — derived from [data-theme="dark"].
// Contrast ratios quoted against .surface.
const NsPalette ns_dark = {
  .surface  = lv_color_hex(0x0C0E13),
  .surface2 = lv_color_hex(0x1A2130),  // was 0x10141C
  .surface3 = lv_color_hex(0x232C3D),  // was 0x161B26 — tiles were ~2 RGB565
                                       // steps off the background, i.e. invisible
  .text     = lv_color_hex(0xF2F6FA),  // was 0xE8ECF2 — 17.4:1
  .textdim  = lv_color_hex(0xA3B0C2),  // was 0x6B7585 — 4.2:1 -> 8.6:1. The single
                                       // biggest legibility win: every secondary
                                       // label on the screen uses this.
  .accent   = lv_color_hex(0x45E0D4),  // was 0x3CC6BD
  .border   = lv_color_hex(0x3A4763),  // was 0x1C2230 — this is the ring track;
                                       // at the old value the unfilled arc simply
                                       // did not render to the eye
  .charging = lv_color_hex(0x45E0D4),  // == accent
  .error    = lv_color_hex(0xFF7A70),  // was 0xF06E66
  .warning  = lv_color_hex(0xFFBE55),  // was 0xE7A948
  .sleep    = lv_color_hex(0x8FC0E8),  // was 0x7DA7C8
  .success  = lv_color_hex(0x6EE88A),  // was 0x5DC975
  .soc      = lv_color_hex(0x5AA9E6),
};

// light — derived from [data-theme="light"] (the web GUI's :root fallback).
// Here the retune runs the other way: darken, since the ground is white.
const NsPalette ns_light = {
  .surface  = lv_color_hex(0xFFFFFF),
  .surface2 = lv_color_hex(0xE3ECEB),  // was 0xEEF4F3
  .surface3 = lv_color_hex(0xD2E0DE),  // was 0xDDE7E6
  .text     = lv_color_hex(0x13202B),
  .textdim  = lv_color_hex(0x47555B),  // was 0x5B6B72 — 5.5:1 -> 8.2:1
  .accent   = lv_color_hex(0x0B7E7B),  // was 0x0F9B98 — mid teal on white is weak
  .border   = lv_color_hex(0xB6C6C4),  // was 0xE4EAE9 — invisible ring track
  .charging = lv_color_hex(0x0B7E7B),  // == accent
  .error    = lv_color_hex(0xC22E26),  // was 0xD6453D
  .warning  = lv_color_hex(0xB86F14),  // was 0xD98A2B
  .sleep    = lv_color_hex(0x4E7C9E),  // was 0x6792B3
  .success  = lv_color_hex(0x1F8A42),  // was 0x2EA052
  .soc      = lv_color_hex(0x2E7FBF),
};

const NsPalette *ns_active = &ns_dark;

void ns_set_theme(bool light)
{
  ns_active = light ? &ns_light : &ns_dark;
}

#endif // ENABLE_SCREEN_LVGL_TFT
