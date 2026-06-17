// src/lvgl_tft/boot_screen.cpp — see boot_screen.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>

#include "boot_screen.h"

// Nightshift palette (matches charge_screen.cpp).
#define COL_BG     lv_color_hex(0x0B1220)
#define COL_TRACK  lv_color_hex(0x16202E)
#define COL_ACCENT lv_color_hex(0x38BDF8)
#define COL_TEXT   lv_color_hex(0xE6EDF3)
#define COL_DIM    lv_color_hex(0x7D8A99)

static lv_obj_t *boot_scr = nullptr;
static lv_obj_t *bar      = nullptr;
static lv_obj_t *boot_msg = nullptr;

void boot_screen_build()
{
  boot_scr = lv_obj_create(NULL);
  lv_scr_load(boot_scr);
  lv_obj_set_style_bg_color(boot_scr, COL_BG, 0);
  lv_obj_clear_flag(boot_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(boot_scr);
  lv_label_set_text(title, "OpenEVSE");
  lv_obj_set_style_text_color(title, COL_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -42);

  lv_obj_t *sub = lv_label_create(boot_scr);
  lv_label_set_text(sub, "STARTING");
  lv_obj_set_style_text_color(sub, COL_ACCENT, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, 0);
  lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  bar = lv_bar_create(boot_scr);
  lv_obj_set_size(bar, 300, 10);
  lv_obj_align(bar, LV_ALIGN_CENTER, 0, 52);
  lv_obj_set_style_bg_color(bar, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, COL_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);

  boot_msg = lv_label_create(boot_scr);
  lv_label_set_text(boot_msg, "");
  lv_obj_set_style_text_color(boot_msg, COL_DIM, 0);
  lv_obj_set_style_text_font(boot_msg, &lv_font_montserrat_14, 0);
  lv_obj_align(boot_msg, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void boot_screen_update(int percent, const char *msg)
{
  if (!bar) return;
  if (percent < 0) percent = 0; else if (percent > 100) percent = 100;
  lv_bar_set_value(bar, percent, LV_ANIM_OFF);
  lv_label_set_text(boot_msg, (msg && msg[0]) ? msg : "");
}

void boot_screen_destroy()
{
  if (boot_scr) {
    lv_obj_del(boot_scr);
    boot_scr = nullptr;
    bar = nullptr;
    boot_msg = nullptr;
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
