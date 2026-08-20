// src/lvgl_tft/standby_screen.cpp — see standby_screen.h.
//
// Same skeleton as charge_screen.cpp, and deliberately so: shared RING_*/TILE_*
// geometry means the ring and the tile column do not move when the display
// switches between them. What differs is what is worth showing when nothing is
// happening -- no live power, no pilot line, no SoC ring; just state, the clock
// and the running totals.
//
// This screen is rendered at the standby brightness, so contrast is doing more
// work here than on the charge screen, not less. Nothing on it uses the 14px
// size except the host/IP line.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "standby_screen.h"
#include "screen_common.h"   // state_word, fmt_temp, shared geometry
#include "nightshift.h"

#define COL_BG     NS_SURFACE
#define COL_CARD   NS_SURFACE3
#define COL_TRACK  NS_BORDER
#define COL_TEXT   NS_TEXT
#define COL_DIM    NS_TEXTDIM
#define COL_ACCENT NS_ACCENT
#define COL_OK     NS_SUCCESS
#define COL_FAULT  NS_ERROR
#define COL_WARN   NS_WARNING

static lv_obj_t *standby_scr  = nullptr;
static lv_obj_t *arc          = nullptr;
static lv_obj_t *state_lbl    = nullptr;
static lv_obj_t *clock_lbl    = nullptr;
static lv_obj_t *chip_row     = nullptr;
static lv_obj_t *chip_temp    = nullptr;
static lv_obj_t *chip_wifi    = nullptr;
static lv_obj_t *hostip_lbl   = nullptr;
static lv_obj_t *tile_value[3] = {nullptr, nullptr, nullptr};

// One stat tile: a rounded card with a dim caption and one big value. Matches
// the charge screen's tiles so the column doesn't shift between screens.
static void make_tile(lv_obj_t *parent, int idx, lv_coord_t y, const char *title)
{
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_set_size(tile, TILE_W, TILE_H);
  lv_obj_set_pos(tile, TILE_X, y);
  lv_obj_set_style_bg_color(tile, COL_CARD, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_radius(tile, 10, 0);
  lv_obj_set_style_pad_all(tile, 8, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(tile);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, COL_DIM, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *v = lv_label_create(tile);
  lv_label_set_text(v, "--");
  lv_obj_set_style_text_color(v, COL_TEXT, 0);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_36, 0);
  lv_obj_set_width(v, TILE_W - 16);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  tile_value[idx] = v;
}

static lv_obj_t *make_chip(lv_obj_t *row)
{
  lv_obj_t *c = lv_label_create(row);
  lv_label_set_text(c, "");
  lv_obj_set_style_text_font(c, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(c, COL_TEXT, 0);
  lv_obj_set_style_bg_color(c, COL_CARD, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 8, 0);
  lv_obj_set_style_pad_hor(c, 8, 0);
  lv_obj_set_style_pad_ver(c, 4, 0);
  return c;
}

static void chip_set(lv_obj_t *c, const char *text, lv_color_t bg, lv_color_t fg)
{
  lv_label_set_text(c, text);
  lv_obj_set_style_bg_color(c, bg, 0);
  lv_obj_set_style_text_color(c, fg, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
}

void standby_screen_build()
{
  // Load the new screen before deleting the old (active-screen-delete panics LVGL).
  lv_obj_t *old = standby_scr;
  lv_obj_t *scr = lv_obj_create(NULL);
  standby_scr = scr;
  lv_scr_load(scr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // --- Top strip, line 1: clock (left) + status chips (right) ---
  clock_lbl = lv_label_create(scr);
  lv_label_set_text(clock_lbl, "");
  lv_obj_set_style_text_color(clock_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_width(clock_lbl, 220);
  lv_obj_set_style_text_align(clock_lbl, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(clock_lbl, LV_ALIGN_TOP_LEFT, 12, 6);

  chip_row = lv_obj_create(scr);
  lv_obj_remove_style_all(chip_row);
  lv_obj_set_size(chip_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(chip_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(chip_row, 6, 0);
  lv_obj_clear_flag(chip_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(chip_row, LV_ALIGN_TOP_RIGHT, -12, 4);
  chip_temp = make_chip(chip_row);
  chip_wifi = make_chip(chip_row);

  // --- Footer: hostname / IP, centred across the full width below everything ---
  hostip_lbl = lv_label_create(scr);
  lv_label_set_text(hostip_lbl, "");
  lv_obj_set_style_text_color(hostip_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(hostip_lbl, &lv_font_montserrat_14, 0);
  // No explicit width: BOTTOM_MID centres the label's own box, and the address
  // is static, so there is no digit jitter to pin down here.
  lv_obj_align(hostip_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

  // --- Ring (left) — same geometry as the charge screen's power ring ---
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, RING_SIZE, RING_SIZE);
  lv_obj_align(arc, LV_ALIGN_LEFT_MID, RING_LEFT, RING_Y);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 100);
  // Nothing is flowing on this screen, so the ring stays an empty track: it is
  // the state word's frame here, not a gauge.
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, RING_BAND, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, RING_BAND, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COL_ACCENT, LV_PART_INDICATOR);

  state_lbl = lv_label_create(scr);
  lv_label_set_long_mode(state_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(state_lbl, STATE_WORD_FIT_W);
  lv_obj_set_style_text_align(state_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(state_lbl, "");
  lv_obj_set_style_text_color(state_lbl, COL_ACCENT, 0);
  lv_obj_set_style_text_font(state_lbl, &lv_font_montserrat_28, 0);
  lv_obj_align_to(state_lbl, arc, LV_ALIGN_CENTER, 0, 0);

  // --- Totals (right) — the same three slots the charge screen uses when idle,
  // lifted by the height of the second top-strip line this screen doesn't have
  // (the address is in the footer), which is also what clears the footer band.
  make_tile(scr, 0, TILE_Y0 - TILE_LIFT, "TODAY");
  make_tile(scr, 1, TILE_Y1 - TILE_LIFT, "THIS WEEK");
  make_tile(scr, 2, TILE_Y2 - TILE_LIFT, "LIFETIME");

  if (old) {
    lv_obj_del(old);
  }
}

// kWh at a precision that fits the tile at 36px. Matches the charge screen.
static void format_kwh(char *buf, size_t len, double kwh)
{
  if (kwh >= 1000.0)     snprintf(buf, len, "%.0f kWh", kwh);
  else if (kwh >= 100.0) snprintf(buf, len, "%.1f kWh", kwh);
  else                   snprintf(buf, len, "%.2f kWh", kwh);
}

void standby_screen_update(const StandbyScreenData &d)
{
  char buf[64];

  lv_color_t accent;
  const char *word = state_word(d.evse_state, &accent);

  // Length-adaptive font, mirroring the charge screen: keep the large size for
  // words that render inside the ring, drop one size for the wide ones so they
  // stay on a single line.
  lv_point_t sz;
  lv_txt_get_size(&sz, word, &lv_font_montserrat_28, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const lv_font_t *font = (sz.x <= STATE_WORD_FIT_W) ? &lv_font_montserrat_28
                                                     : &lv_font_montserrat_20;
  lv_obj_set_style_text_font(state_lbl, font, 0);

  lv_label_set_text(state_lbl, word);
  lv_obj_set_style_text_color(state_lbl, accent, 0);
  lv_obj_align_to(state_lbl, arc, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);

  lv_label_set_text(clock_lbl, d.clock ? d.clock : "");

  // Status chips, same semantics as the charge screen.
  if (d.temp_valid) {
    // fmt_temp pads two trailing spaces for the old top-strip layout; a pill
    // sizes to its text, so trim them.
    int tn = fmt_temp(buf, sizeof(buf), d.temp_c, d.temp_fahrenheit);
    while (tn > 0 && buf[tn - 1] == ' ') buf[--tn] = '\0';
    if (d.temp_c >= 50.0f) chip_set(chip_temp, buf, COL_WARN, COL_BG);
    else                   chip_set(chip_temp, buf, COL_CARD, COL_TEXT);
  } else {
    lv_obj_add_flag(chip_temp, LV_OBJ_FLAG_HIDDEN);
  }

  if (d.wifi_client) {
    if (d.wifi_connected) {
      snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %d%%", d.wifi_pct);
      chip_set(chip_wifi, buf, COL_CARD, COL_TEXT);
    } else {
      chip_set(chip_wifi, LV_SYMBOL_WIFI " --", COL_FAULT, COL_BG);
    }
  } else {
    snprintf(buf, sizeof(buf), "AP %d", d.sta_count);
    chip_set(chip_wifi, buf, COL_ACCENT, COL_BG);
  }

  format_kwh(buf, sizeof(buf), d.today_kwh);
  lv_label_set_text(tile_value[0], buf);
  format_kwh(buf, sizeof(buf), d.week_kwh);
  lv_label_set_text(tile_value[1], buf);
  format_kwh(buf, sizeof(buf), d.total_kwh);
  lv_label_set_text(tile_value[2], buf);

  snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_BULLET "  %s",
           d.hostname ? d.hostname : "", d.ip ? d.ip : "");
  lv_label_set_text(hostip_lbl, buf);
}

void standby_screen_destroy()
{
  if (standby_scr) {
    lv_obj_del(standby_scr);
    standby_scr  = nullptr;
    arc = state_lbl = clock_lbl = hostip_lbl = nullptr;
    chip_row = chip_temp = chip_wifi = nullptr;
    for (int i = 0; i < 3; i++) {
      tile_value[i] = nullptr;
    }
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
