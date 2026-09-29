// src/lvgl_tft/mark_img.h — the OpenEVSE charge point mark, as LVGL images.
//
// Generated from the web UI's mark.svg by scripts/gen_lvgl_mark.mjs; see that
// script for why this is two alpha masks rather than one bitmap.
#ifndef __MARK_IMG_H
#define __MARK_IMG_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <lvgl.h>

// Enclosure + cord. Tint with the theme accent.
extern const lv_img_dsc_t mark_shell_img;

// The bolt. Tint with the fixed brand violet (NS_MARK_BOLT).
extern const lv_img_dsc_t mark_bolt_img;

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __MARK_IMG_H
