// Generated fault mark masks -- see scripts/gen_lvgl_mark.mjs.
#ifndef __MARK_FAULT_IMG_H
#define __MARK_FAULT_IMG_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <lvgl.h>

// Two stacked LV_IMG_CF_ALPHA_8BIT masks sharing one origin: the enclosure,
// cord and plug, and the alert glyph inside them. Draw the shell in the theme
// accent and the glyph in the error colour, and set img_recolor_opa on both --
// LVGL reads img_recolor only when the opa is above zero.
extern const lv_img_dsc_t mark_fault_shell_img;
extern const lv_img_dsc_t mark_fault_alert_img;

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __MARK_FAULT_IMG_H
