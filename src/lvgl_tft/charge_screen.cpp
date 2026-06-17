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

static lv_obj_t *charge_scr   = nullptr;  // the screen object (for destroy on switch)
static lv_obj_t *arc          = nullptr;
static lv_obj_t *big_value    = nullptr;  // kW number, centre of ring (charging only)
static lv_obj_t *big_unit     = nullptr;  // "kW" (charging only)
static lv_obj_t *center_state = nullptr;  // state word in ring centre (not charging)
static lv_obj_t *pilot_lbl    = nullptr;  // pilot / allowed current, below the ring
static lv_obj_t *datetime_lbl = nullptr;
static lv_obj_t *topright_lbl = nullptr;  // temp + wifi% + car
static lv_obj_t *msg_lbl      = nullptr;  // transient message (bottom-centre)
static lv_obj_t *host_lbl     = nullptr;  // hostname (bottom-left)
static lv_obj_t *ip_lbl       = nullptr;  // IP (bottom-right)
static lv_obj_t *elapsed_val  = nullptr;
static lv_obj_t *delivered_val= nullptr;
static lv_obj_t *va_val       = nullptr;

// One stat tile: a rounded card with a dim title and a bright value.
static void make_tile(lv_obj_t *parent, const char *title, lv_obj_t **value_out,
                      lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_set_size(tile, w, h);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_style_bg_color(tile, COL_CARD, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_radius(tile, 10, 0);
  lv_obj_set_style_pad_all(tile, 10, 0);
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
  // Own LVGL screen, loaded now (the previous screen is on a separate object that
  // the caller deletes after this returns).
  lv_obj_t *scr = lv_obj_create(NULL);
  charge_scr = scr;
  lv_scr_load(scr);
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

  // --- Power ring (left) ---
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

  // --- Ring centre: state word (idle) OR big kW value (charging) ---
  center_state = lv_label_create(scr);
  lv_label_set_long_mode(center_state, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(center_state, 180);
  lv_obj_set_style_text_align(center_state, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(center_state, "");
  lv_obj_set_style_text_color(center_state, COL_ACCENT, 0);
  lv_obj_set_style_text_font(center_state, &lv_font_montserrat_28, 0);
  lv_obj_align_to(center_state, arc, LV_ALIGN_CENTER, 0, 0);

  big_value = lv_label_create(scr);
  lv_label_set_text(big_value, "0.00");
  lv_obj_set_style_text_color(big_value, COL_TEXT, 0);
  lv_obj_set_style_text_font(big_value, &lv_font_montserrat_48, 0);
  lv_obj_align_to(big_value, arc, LV_ALIGN_CENTER, 0, -12);
  lv_obj_add_flag(big_value, LV_OBJ_FLAG_HIDDEN);

  big_unit = lv_label_create(scr);
  lv_label_set_text(big_unit, "kW");
  lv_obj_set_style_text_color(big_unit, COL_DIM, 0);
  lv_obj_set_style_text_font(big_unit, &lv_font_montserrat_20, 0);
  lv_obj_align_to(big_unit, big_value, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
  lv_obj_add_flag(big_unit, LV_OBJ_FLAG_HIDDEN);

  // Pilot / allowed charge current, directly below the ring (shown in all states).
  pilot_lbl = lv_label_create(scr);
  lv_label_set_text(pilot_lbl, "");
  lv_obj_set_style_text_color(pilot_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(pilot_lbl, &lv_font_montserrat_20, 0);
  lv_obj_align_to(pilot_lbl, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, -14);

  // --- Stat tiles (right column) ---
  const lv_coord_t TX = 248, TW = 226, TH = 70;
  make_tile(scr, "ELAPSED",      &elapsed_val,   TX, 54,  TW, TH);
  make_tile(scr, "DELIVERED",    &delivered_val, TX, 132, TW, TH);
  make_tile(scr, "VOLTS / AMPS", &va_val,        TX, 210, TW, TH);

  // --- Bottom row: hostname (left), IP (right), transient message (centre) ---
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

  msg_lbl = lv_label_create(scr);
  lv_label_set_text(msg_lbl, "");
  lv_obj_set_style_text_color(msg_lbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(msg_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_add_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
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

// RSSI (dBm) -> signal %, the usual piecewise mapping.
static int wifi_percent(int rssi)
{
  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;
  return 2 * (rssi + 100);
}

void charge_screen_update(const ChargeScreenData &d)
{
  char buf[48];

  // State -> ring colour. Charging shows kW in the centre; everything else shows
  // the state word in the centre (the headline when there's no live power).
  lv_color_t accent;
  const char *word = state_word(d.evse_state, &accent);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);

  if (d.charging) {
    lv_obj_add_flag(center_state, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(big_value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(big_unit, LV_OBJ_FLAG_HIDDEN);
    if (d.power_kw < 10)       snprintf(buf, sizeof(buf), "%.2f", d.power_kw);
    else if (d.power_kw < 100) snprintf(buf, sizeof(buf), "%.1f", d.power_kw);
    else                       snprintf(buf, sizeof(buf), "%.0f", d.power_kw);
    lv_label_set_text(big_value, buf);
    lv_obj_align_to(big_value, arc, LV_ALIGN_CENTER, 0, -12);
    lv_obj_align_to(big_unit, big_value, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
  } else {
    lv_obj_add_flag(big_value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(big_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(center_state, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(center_state, word);
    lv_obj_set_style_text_color(center_state, accent, 0);
    lv_obj_align_to(center_state, arc, LV_ALIGN_CENTER, 0, 0);
  }

  // Ring: actual amps when charging, else the pilot setpoint, as % of full scale.
  float ring_a = d.charging ? d.amps : (float)d.pilot_a;
  int ring = (int)(ring_a / RING_FULL_SCALE_A * 100.0f);
  if (ring < 0) ring = 0; else if (ring > 100) ring = 100;
  lv_arc_set_value(arc, ring);

  // Pilot / allowed current, below the ring (always).
  snprintf(buf, sizeof(buf), "Pilot %dA", d.pilot_a);
  lv_label_set_text(pilot_lbl, buf);
  lv_obj_align_to(pilot_lbl, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, -14);

  // Top-left: date/time.
  lv_label_set_text(datetime_lbl, d.datetime ? d.datetime : "");

  // Top-right: temp + wifi% + car.
  char tr[48]; tr[0] = '\0';
  size_t n = 0;
  if (d.temp_valid) n += snprintf(tr + n, sizeof(tr) - n, "%.1fC  ", d.temp_c);
  if (d.wifi_client) {
    if (d.wifi_connected) n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " %d%%", wifi_percent(d.rssi));
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

  // Bottom row: a transient message takes over the centre and hides host/IP;
  // otherwise hostname (left) + IP (right).
  if (d.msg_line && d.msg_line[0]) {
    lv_label_set_text(msg_lbl, d.msg_line);
    lv_obj_clear_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(host_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ip_lbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(host_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ip_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(host_lbl, d.hostname ? d.hostname : "");
    lv_label_set_text(ip_lbl, d.ip ? d.ip : "");
  }
}

void charge_screen_destroy()
{
  if (charge_scr) {
    lv_obj_del(charge_scr);
    charge_scr = nullptr;
    arc = big_value = big_unit = center_state = pilot_lbl = nullptr;
    datetime_lbl = topright_lbl = msg_lbl = host_lbl = ip_lbl = nullptr;
    elapsed_val = delivered_val = va_val = nullptr;
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
