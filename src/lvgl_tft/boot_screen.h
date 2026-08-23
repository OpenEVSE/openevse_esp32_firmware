// src/lvgl_tft/boot_screen.h — startup splash for the stock TFT LVGL UI.
// Shown when the panel comes up, then LcdTask hands off to the charge screen.
#ifndef __BOOT_SCREEN_H
#define __BOOT_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

// Create + load a dedicated LVGL screen with the splash. Call once at panel init.
void boot_screen_build();

// Update the progress bar (0..100) and the message line.
void boot_screen_update(int percent, const char *msg);

// Delete the splash screen (call after the charge screen is loaded).
void boot_screen_destroy();

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __BOOT_SCREEN_H
