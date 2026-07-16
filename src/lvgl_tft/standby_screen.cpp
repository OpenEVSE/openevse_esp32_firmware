// src/lvgl_tft/standby_screen.cpp — see standby_screen.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "standby_screen.h"
#include "screen_common.h"   // state_word, wifi_percent
#include "nightshift.h"

#define COL_BG     NS_SURFACE
#define COL_TRACK  NS_BORDER
#define COL_TEXT   NS_TEXT
#define COL_DIM    NS_TEXTDIM
#define COL_ACCENT NS_ACCENT

static lv_obj_t *standby_scr  = nullptr;
static lv_obj_t *arc          = nullptr;
static lv_obj_t *state_lbl    = nullptr;
static lv_obj_t *clock_lbl    = nullptr;
static lv_obj_t *topright_lbl = nullptr;
static lv_obj_t *today_val    = nullptr;
static lv_obj_t *total_val    = nullptr;
static lv_obj_t *host_lbl     = nullptr;
static lv_obj_t *ip_lbl       = nullptr;

void standby_screen_build()
{
  // Load the new screen before deleting the old (active-screen-delete panics LVGL).
  lv_obj_t *old = standby_scr;
  lv_obj_t *scr = lv_obj_create(NULL);
  standby_scr = scr;
  lv_scr_load(scr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Top strip: clock (left), temp + wifi (right).
  clock_lbl = lv_label_create(scr);
  lv_label_set_text(clock_lbl, "");
  lv_obj_set_style_text_color(clock_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(clock_lbl, LV_ALIGN_TOP_LEFT, 12, 8);

  topright_lbl = lv_label_create(scr);
  lv_label_set_text(topright_lbl, "");
  lv_obj_set_style_text_color(topright_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(topright_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(topright_lbl, LV_ALIGN_TOP_RIGHT, -12, 8);

  // Ring on the left (same geometry/style as the charge screen).
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, 200, 200);
  lv_obj_align(arc, LV_ALIGN_LEFT_MID, 18, 8);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COL_ACCENT, LV_PART_INDICATOR);

  state_lbl = lv_label_create(scr);
  lv_label_set_long_mode(state_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(state_lbl, 180);
  lv_obj_set_style_text_align(state_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(state_lbl, "");
  lv_obj_set_style_text_color(state_lbl, COL_ACCENT, 0);
  lv_obj_set_style_text_font(state_lbl, &lv_font_montserrat_28, 0);
  lv_obj_align_to(state_lbl, arc, LV_ALIGN_CENTER, 0, 0);

  // Totals stacked + right-aligned on the right half.
  lv_obj_t *today_t = lv_label_create(scr);
  lv_label_set_text(today_t, "TODAY");
  lv_obj_set_style_text_color(today_t, COL_DIM, 0);
  lv_obj_set_style_text_font(today_t, &lv_font_montserrat_14, 0);
  lv_obj_align(today_t, LV_ALIGN_RIGHT_MID, -24, -56);

  today_val = lv_label_create(scr);
  lv_label_set_text(today_val, "-- kWh");
  lv_obj_set_style_text_color(today_val, COL_ACCENT, 0);
  lv_obj_set_style_text_font(today_val, &lv_font_montserrat_28, 0);
  lv_obj_align(today_val, LV_ALIGN_RIGHT_MID, -24, -28);

  lv_obj_t *total_t = lv_label_create(scr);
  lv_label_set_text(total_t, "TOTAL");
  lv_obj_set_style_text_color(total_t, COL_DIM, 0);
  lv_obj_set_style_text_font(total_t, &lv_font_montserrat_14, 0);
  lv_obj_align(total_t, LV_ALIGN_RIGHT_MID, -24, 24);

  total_val = lv_label_create(scr);
  lv_label_set_text(total_val, "-- kWh");
  lv_obj_set_style_text_color(total_val, COL_TEXT, 0);
  lv_obj_set_style_text_font(total_val, &lv_font_montserrat_28, 0);
  lv_obj_align(total_val, LV_ALIGN_RIGHT_MID, -24, 52);

  // Bottom corners: hostname (left), IP (right).
  host_lbl = lv_label_create(scr);
  lv_label_set_text(host_lbl, "");
  lv_obj_set_style_text_color(host_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(host_lbl, LV_ALIGN_BOTTOM_LEFT, 12, -6);

  ip_lbl = lv_label_create(scr);
  lv_label_set_text(ip_lbl, "");
  lv_obj_set_style_text_color(ip_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(ip_lbl, LV_ALIGN_BOTTOM_RIGHT, -12, -6);

  if (old) {
    lv_obj_del(old);
  }
}

void standby_screen_update(const StandbyScreenData &d)
{
  char buf[48];

  lv_color_t accent;
  const char *word = state_word(d.evse_state, &accent);
  lv_label_set_text(state_lbl, word);
  lv_obj_set_style_text_color(state_lbl, accent, 0);
  lv_obj_align_to(state_lbl, arc, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);

  lv_label_set_text(clock_lbl, d.clock ? d.clock : "");

  char tr[48]; tr[0] = '\0';
  size_t n = 0;
  if (d.temp_valid) n += fmt_temp(tr + n, sizeof(tr) - n, d.temp_c, d.temp_fahrenheit);
  if (d.wifi_client) {
    if (d.wifi_connected) n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " %d%%", wifi_percent(d.rssi));
    else                  n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " --");
  } else {
    n += snprintf(tr + n, sizeof(tr) - n, "AP:%d", d.sta_count);
  }
  lv_label_set_text(topright_lbl, tr);

  snprintf(buf, sizeof(buf), "%.1f kWh", d.today_kwh);
  lv_label_set_text(today_val, buf);
  snprintf(buf, sizeof(buf), "%.0f kWh", d.total_kwh);
  lv_label_set_text(total_val, buf);

  lv_label_set_text(host_lbl, d.hostname ? d.hostname : "");
  lv_label_set_text(ip_lbl, d.ip ? d.ip : "");
}

void standby_screen_destroy()
{
  if (standby_scr) {
    lv_obj_del(standby_scr);
    standby_scr  = nullptr;
    arc = state_lbl = clock_lbl = topright_lbl = nullptr;
    today_val = total_val = host_lbl = ip_lbl = nullptr;
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
