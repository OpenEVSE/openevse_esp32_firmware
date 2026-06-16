// src/lvgl_tft/charge_screen.cpp — see charge_screen.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "charge_screen.h"
#include "openevse.h"   // OPENEVSE_STATE_*

// Nightshift palette (approximation; refine to the EEZ theme hexes later).
#define COL_BG      lv_color_hex(0x0B1220) // deep surface
#define COL_CARD    lv_color_hex(0x16202E) // tile surface
#define COL_ACCENT  lv_color_hex(0x38BDF8) // cyan — connected/idle
#define COL_OK      lv_color_hex(0x34D399) // green — charging
#define COL_FAULT   lv_color_hex(0xF87171) // red — fault
#define COL_TEXT    lv_color_hex(0xE6EDF3)
#define COL_DIM     lv_color_hex(0x7D8A99)

// Ring full-scale (amps). The ring is indicative, not a hard gauge.
#define RING_FULL_SCALE_A 48.0f

static lv_obj_t *arc          = nullptr;
static lv_obj_t *big_value    = nullptr;  // kW (charging) or A (idle)
static lv_obj_t *big_unit     = nullptr;
static lv_obj_t *status_word  = nullptr;
static lv_obj_t *datetime_lbl = nullptr;
static lv_obj_t *topright_lbl = nullptr;  // temp + wifi + car
static lv_obj_t *msg_lbl      = nullptr;
static lv_obj_t *elapsed_val  = nullptr;
static lv_obj_t *delivered_val= nullptr;
static lv_obj_t *va_val       = nullptr;

// One stat tile: a rounded card with a dim title and a bright value. Returns the
// value label via value_out.
static void make_tile(lv_obj_t *parent, const char *title, lv_obj_t **value_out,
                      lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_set_size(tile, w, h);
  lv_obj_set_pos(tile, x, y);
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
  lv_obj_set_style_text_font(v, &lv_font_montserrat_28, 0);
  lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  *value_out = v;
}

void charge_screen_build()
{
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // --- Top strip ---
  datetime_lbl = lv_label_create(scr);
  lv_label_set_text(datetime_lbl, "");
  lv_obj_set_style_text_color(datetime_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(datetime_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(datetime_lbl, LV_ALIGN_TOP_LEFT, 12, 8);

  topright_lbl = lv_label_create(scr);
  lv_label_set_text(topright_lbl, "");
  lv_obj_set_style_text_color(topright_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(topright_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(topright_lbl, LV_ALIGN_TOP_RIGHT, -12, 8);

  // --- Power ring + big value (left) ---
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
  lv_obj_set_style_arc_color(arc, COL_CARD, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COL_ACCENT, LV_PART_INDICATOR);

  big_value = lv_label_create(scr);
  lv_label_set_text(big_value, "0.00");
  lv_obj_set_style_text_color(big_value, COL_TEXT, 0);
  lv_obj_set_style_text_font(big_value, &lv_font_montserrat_48, 0);
  lv_obj_align_to(big_value, arc, LV_ALIGN_CENTER, 0, -10);

  big_unit = lv_label_create(scr);
  lv_label_set_text(big_unit, "kW");
  lv_obj_set_style_text_color(big_unit, COL_DIM, 0);
  lv_obj_set_style_text_font(big_unit, &lv_font_montserrat_20, 0);
  lv_obj_align_to(big_unit, big_value, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  // --- Status word (top of right column) ---
  status_word = lv_label_create(scr);
  lv_label_set_text(status_word, "");
  lv_obj_set_style_text_color(status_word, COL_ACCENT, 0);
  lv_obj_set_style_text_font(status_word, &lv_font_montserrat_28, 0);
  lv_obj_align(status_word, LV_ALIGN_TOP_RIGHT, -16, 34);

  // --- Stat tiles (right column) ---
  const lv_coord_t TX = 250, TW = 214, TH = 56;
  make_tile(scr, "ELAPSED",   &elapsed_val,   TX, 74,  TW, TH);
  make_tile(scr, "DELIVERED", &delivered_val, TX, 138, TW, TH);
  make_tile(scr, "VOLTS / AMPS", &va_val,     TX, 202, TW, TH);

  // --- Message line (bottom) ---
  msg_lbl = lv_label_create(scr);
  lv_label_set_text(msg_lbl, "");
  lv_obj_set_style_text_color(msg_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(msg_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// Map EVSE state -> status word + accent colour.
static const char *state_word(uint8_t s, lv_color_t *colour)
{
  switch (s) {
    case OPENEVSE_STATE_CHARGING:      *colour = COL_OK;     return "CHARGING";
    case OPENEVSE_STATE_CONNECTED:     *colour = COL_ACCENT; return "CONNECTED";
    case OPENEVSE_STATE_SLEEPING:      *colour = COL_DIM;    return "SLEEPING";
    case OPENEVSE_STATE_DISABLED:      *colour = COL_DIM;    return "DISABLED";
    case OPENEVSE_STATE_STARTING:      *colour = COL_ACCENT; return "STARTING";
    case OPENEVSE_STATE_NOT_CONNECTED: *colour = COL_DIM;    return "NOT CONNECTED";
    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:  *colour = COL_FAULT;  return "FAULT";
    default:                           *colour = COL_DIM;    return "--";
  }
}

void charge_screen_update(const ChargeScreenData &d)
{
  char buf[48];

  // Status word + colour, ring colour follows.
  lv_color_t accent;
  const char *word = state_word(d.evse_state, &accent);
  lv_label_set_text(status_word, word);
  lv_obj_set_style_text_color(status_word, accent, 0);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);

  // Ring: actual amps when charging, else the pilot setpoint, as % of full scale.
  float ring_a = d.charging ? d.amps : (float)d.pilot_a;
  int ring = (int)(ring_a / RING_FULL_SCALE_A * 100.0f);
  if (ring < 0) ring = 0; else if (ring > 100) ring = 100;
  lv_arc_set_value(arc, ring);

  // Center value: kW when charging, pilot A otherwise.
  if (d.charging) {
    if (d.power_kw < 10)      snprintf(buf, sizeof(buf), "%.2f", d.power_kw);
    else if (d.power_kw < 100) snprintf(buf, sizeof(buf), "%.1f", d.power_kw);
    else                       snprintf(buf, sizeof(buf), "%.0f", d.power_kw);
    lv_label_set_text(big_value, buf);
    lv_label_set_text(big_unit, "kW");
  } else {
    snprintf(buf, sizeof(buf), "%d", d.pilot_a);
    lv_label_set_text(big_value, buf);
    lv_label_set_text(big_unit, "A");
  }
  lv_obj_align_to(big_value, arc, LV_ALIGN_CENTER, 0, -10);
  lv_obj_align_to(big_unit, big_value, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  // Top-left: date/time.
  lv_label_set_text(datetime_lbl, d.datetime ? d.datetime : "");

  // Top-right: temp + wifi + car, space-separated.
  char tr[48]; tr[0] = '\0';
  size_t n = 0;
  if (d.temp_valid) n += snprintf(tr + n, sizeof(tr) - n, "%.1fC  ", d.temp_c);
  if (d.wifi_client) {
    if (d.wifi_connected) n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " %d", d.rssi);
    else                  n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " --");
  } else {
    n += snprintf(tr + n, sizeof(tr) - n, "AP:%d", d.sta_count);
  }
  if (d.vehicle_connected) n += snprintf(tr + n, sizeof(tr) - n, "  " LV_SYMBOL_CHARGE);
  lv_label_set_text(topright_lbl, tr);

  // Tiles.
  uint32_t h = d.elapsed_s / 3600, m = (d.elapsed_s % 3600) / 60, s = d.elapsed_s % 60;
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  lv_label_set_text(elapsed_val, buf);

  if (d.session_wh >= 1000.0) snprintf(buf, sizeof(buf), "%.2f kWh", d.session_wh / 1000.0);
  else                        snprintf(buf, sizeof(buf), "%.0f Wh", d.session_wh);
  lv_label_set_text(delivered_val, buf);

  snprintf(buf, sizeof(buf), "%.0fV  %.1fA", d.volts, d.amps);
  lv_label_set_text(va_val, buf);

  // Message line.
  lv_label_set_text(msg_lbl, (d.msg_line && d.msg_line[0]) ? d.msg_line : "");
}

#endif // ENABLE_SCREEN_LVGL_TFT
