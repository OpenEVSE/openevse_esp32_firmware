// src/lvgl_tft/nightshift.h — the nightshift palette, exact hexes from the source
// of truth gui-nightshift/src/app.css, shared with the P4 EEZ UI so the stock TFT,
// the P4 panel and the web GUI all match. See
// docs/superpowers/specs/2026-05-31-p4-eez-nightshift-theme.md.
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

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __NIGHTSHIFT_H
