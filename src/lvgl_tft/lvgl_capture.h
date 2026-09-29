// Host-only LVGL screen capture helpers for the native LVGL build.
#ifndef __LVGL_CAPTURE_H
#define __LVGL_CAPTURE_H

#if defined(ENABLE_SCREEN_LVGL_TFT) && defined(EPOXY_DUINO)

bool lvgl_capture_write_samples(const char *out_dir);

#endif
#endif // __LVGL_CAPTURE_H
