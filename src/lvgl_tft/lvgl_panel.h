// src/lvgl_tft/lvgl_panel.h — LVGL + ILI9488 (TFT_eSPI) bring-up for the stock
// OpenEVSE color display. No PSRAM, no DMA: one internal-DRAM partial buffer,
// blocking flush. Validated by the spike (spike/lvgl-tft).
#ifndef __LVGL_PANEL_H
#define __LVGL_PANEL_H

#ifdef ENABLE_SCREEN_LVGL_TFT

// Initialise TFT_eSPI + LVGL and register the display driver. Call once, from
// the LcdTask's first loop() (AFTER networking is up — it breaks the display if
// done earlier). Returns false if the draw buffer could not be allocated, in
// which case no display is registered and the caller must NOT build any UI.
bool lvgl_panel_begin();

#ifdef EPOXY_DUINO
enum LvglPanelDisplayMode {
  LVGL_PANEL_DISPLAY_HEADLESS = 0,
  LVGL_PANEL_DISPLAY_WINDOW
};

void lvgl_panel_set_display_mode(LvglPanelDisplayMode mode);
LvglPanelDisplayMode lvgl_panel_get_display_mode();
const char *lvgl_panel_get_display_mode_name(LvglPanelDisplayMode mode);
void lvgl_panel_pump();

// Write the current native LVGL framebuffer to a binary PPM image. Returns false
// if the headless framebuffer is unavailable or the file could not be written.
bool lvgl_panel_write_ppm(const char *path);
#endif

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __LVGL_PANEL_H
