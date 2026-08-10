// src/lvgl_tft/setup_screen.cpp — see setup_screen.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "setup_screen.h"
#include "nightshift.h"

#define COL_BG     NS_SURFACE
#define COL_ACCENT NS_ACCENT
#define COL_TEXT   NS_TEXT
#define COL_DIM    NS_TEXTDIM

static lv_obj_t *setup_scr = nullptr;

void setup_screen_build(const char *qr_join, const char *ssid,
                        const char *pass, const char *ip)
{
  // Keep any existing setup screen until the new one is loaded, then delete it —
  // never delete the *active* screen first (LVGL's active pointer would dangle).
  lv_obj_t *old = setup_scr;
  setup_scr = lv_obj_create(NULL);
  lv_scr_load(setup_scr);
  lv_obj_set_style_bg_color(setup_scr, COL_BG, 0);
  lv_obj_clear_flag(setup_scr, LV_OBJ_FLAG_SCROLLABLE);

  // QR on a white card (quiet zone) so phones scan it against the dark UI.
  lv_obj_t *card = lv_obj_create(setup_scr);
  lv_obj_set_size(card, 208, 208);
  lv_obj_align(card, LV_ALIGN_LEFT_MID, 20, 0);
  lv_obj_set_style_bg_color(card, lv_color_white(), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *qr = lv_qrcode_create(card, 184, lv_color_black(), lv_color_white());
  lv_obj_center(qr);
  lv_qrcode_update(qr, qr_join, strlen(qr_join));

  // Right column: instructions.
  lv_obj_t *title = lv_label_create(setup_scr);
  lv_label_set_text(title, "WiFi Setup");
  lv_obj_set_style_text_color(title, COL_ACCENT, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -16, 28);

  lv_obj_t *hint = lv_label_create(setup_scr);
  lv_label_set_text(hint, "Scan to join, then\nfollow the prompt");
  lv_obj_set_style_text_color(hint, COL_TEXT, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -16, 74);

  char buf[96];

  lv_obj_t *net = lv_label_create(setup_scr);
  snprintf(buf, sizeof(buf), "Network: %s", ssid ? ssid : "");
  lv_label_set_text(net, buf);
  lv_obj_set_style_text_color(net, COL_DIM, 0);
  lv_obj_set_style_text_font(net, &lv_font_montserrat_14, 0);
  lv_obj_align(net, LV_ALIGN_TOP_RIGHT, -16, 144);

  lv_obj_t *pw = lv_label_create(setup_scr);
  snprintf(buf, sizeof(buf), "Password: %s", pass ? pass : "");
  lv_label_set_text(pw, buf);
  lv_obj_set_style_text_color(pw, COL_DIM, 0);
  lv_obj_set_style_text_font(pw, &lv_font_montserrat_14, 0);
  lv_obj_align(pw, LV_ALIGN_TOP_RIGHT, -16, 168);

  lv_obj_t *url = lv_label_create(setup_scr);
  snprintf(buf, sizeof(buf), "or browse to %s", ip ? ip : "");
  lv_label_set_text(url, buf);
  lv_obj_set_style_text_color(url, COL_DIM, 0);
  lv_obj_set_style_text_font(url, &lv_font_montserrat_14, 0);
  lv_obj_align(url, LV_ALIGN_TOP_RIGHT, -16, 192);

  // New screen is now active; safe to delete the previous instance (theme rebuild).
  if(old) {
    lv_obj_del(old);
  }
}

void setup_screen_destroy()
{
  if (setup_scr) {
    lv_obj_del(setup_scr);
    setup_scr = nullptr;
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
