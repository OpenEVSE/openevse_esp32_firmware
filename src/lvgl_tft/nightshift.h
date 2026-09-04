// src/lvgl_tft/nightshift.h — the nightshift palette, hue-matched to the source of
// truth gui-nightshift/src/app.css and shared in spirit with the P4 EEZ UI. See
// docs/superpowers/specs/2026-05-31-p4-eez-nightshift-theme.md.
//
// NOT hex-identical to app.css. The web tokens are tuned for a phone or monitor at
// arm's length; this panel is a 3.5" ILI9488 read from across a garage, through
// RGB565 quantisation that collapses near-black steps. The tokens below keep the
// web palette's hues but widen the luminance gaps — chiefly textdim, the tile
// surface and the ring track, which were all within a few RGB565 steps of the
// background. Treat app.css as the hue reference, not the value reference.
//
// Two themes (dark = nightshift, light) live as runtime palettes; the NS_* macros
// resolve the *active* palette so every call site is theme-agnostic. Swap with
// ns_set_theme(); the renderer rebuilds the current screen to repaint.
#ifndef __NIGHTSHIFT_H
#define __NIGHTSHIFT_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <lvgl.h>

struct NsPalette
{
  lv_color_t surface;   // screen base
  lv_color_t surface2;
  lv_color_t surface3;  // cards / tiles
  lv_color_t text;
  lv_color_t textdim;
  lv_color_t accent;    // brand teal
  lv_color_t border;    // dividers / ring track
  lv_color_t charging;  // == accent (web GUI power ring)
  lv_color_t error;
  lv_color_t warning;
  lv_color_t sleep;
  lv_color_t success;
  lv_color_t soc;       // vehicle state-of-charge ring — deliberately not accent
                        // (teal) or success (green), both already in use on the
                        // power ring, so the two rings never read as one.
};

extern const NsPalette ns_dark;   // nightshift (default)
extern const NsPalette ns_light;
extern const NsPalette *ns_active; // points at one of the above

// Select the active palette. Does NOT repaint already-built widgets — the caller
// (lcd_lvgl) rebuilds the current screen after switching.
void ns_set_theme(bool light);

// All draw code reads colours through these, resolving the active palette.
#define NS_SURFACE   (ns_active->surface)
#define NS_SURFACE2  (ns_active->surface2)
#define NS_SURFACE3  (ns_active->surface3)
#define NS_TEXT      (ns_active->text)
#define NS_TEXTDIM   (ns_active->textdim)
#define NS_ACCENT    (ns_active->accent)
#define NS_BORDER    (ns_active->border)
#define NS_CHARGING  (ns_active->charging)
#define NS_ERROR     (ns_active->error)
#define NS_WARNING   (ns_active->warning)
#define NS_SLEEP     (ns_active->sleep)
#define NS_SUCCESS   (ns_active->success)
#define NS_SOC       (ns_active->soc)

// The brand mark's bolt. Deliberately NOT in the palette and NOT theme-dependent:
// it is the one fixed colour in the mark, matching --mark-bolt in the web UI's
// app.css. Violet because red/amber/green/blue are already error/warning/success/
// sleep here, and a bolt in any of those would read as a status rather than a
// brand. Clears 3:1 on both the dark and light surface, so one value serves both.
#define NS_MARK_BOLT (lv_color_hex(0x8b5cf6))

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __NIGHTSHIFT_H
