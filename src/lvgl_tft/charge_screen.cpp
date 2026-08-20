// src/lvgl_tft/charge_screen.cpp — see charge_screen.h.
//
// Layout (480x320). The panel is read from across a garage, so the rule
// throughout is one big number per region and generous colour separation:
//
//   y=6    clock (18px)                              status chips (18px)
//   y=32   transient message (18px), when there is one -- the address is
//          reference material and lives on the standby screen instead
//   y=56   +--------------------------+  +-----------------------------+
//          |  SoC ring (outer, 212)   |  |  tile 1   caption / value   |
//          |  power ring (inner, 188) |  +-----------------------------+
//          |    kW  or  STATE WORD    |  |  tile 2                     |
//          +--------------------------+  +-----------------------------+
//   y=262    pilot amps + what set them  |  tile 3                     |
//   y=290    SoC % + range               +-----------------------------+
//
// The tile column swaps content on session_active: session figures while a
// vehicle is plugged in, lifetime totals when idle (the tiles otherwise sit at
// "--" and 00:00:00 for most of the charger's life).
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "charge_screen.h"
#include "openevse.h"     // OPENEVSE_STATE_*
#include "nightshift.h"   // exact nightshift palette
#include "screen_common.h" // state_word / fmt_temp, shared with standby

#define COL_BG      NS_SURFACE   // screen base
#define COL_CARD    NS_SURFACE3  // tile surface
#define COL_TRACK   NS_BORDER    // ring track
#define COL_ACCENT  NS_ACCENT    // connected / starting
#define COL_OK      NS_SUCCESS   // charging
#define COL_FAULT   NS_ERROR     // fault
#define COL_WARN    NS_WARNING

// How far below the throttle setpoint the chip starts warning. Wide enough to
// be a heads-up rather than a coincidence with the throttle kicking in.
#define TEMP_WARN_MARGIN_C 10.0f

#define COL_SLEEP   NS_SLEEP     // sleeping
#define COL_TEXT    NS_TEXT
#define COL_DIM     NS_TEXTDIM
#define COL_SOC     NS_SOC

// Ring full-scale fallback (amps), used only when the configured max current is
// not known. The ring is indicative, not a hard gauge.
#define RING_FULL_SCALE_A 48.0f

// Power-ring geometry (RING_*) and the tile column (TILE_*) come from
// screen_common.h so the standby screen lands them in the same place. Only the
// SoC ring is local to this screen -- it shares the power ring's centre, sitting
// just outside it so the two read as one instrument.
#define SOC_ARC_SIZE  (RING_SIZE + 24)
#define SOC_ARC_LEFT  (RING_LEFT - 12)
#define SOC_BAND        8

#define LEFT_COL_W    236       // text under the rings spans the left column

// Power-ring tween duration. Long enough to read as motion at 1 Hz updates,
// short enough to have settled before the next sample arrives.
#define ARC_ANIM_MS 600

static lv_obj_t *charge_scr   = nullptr;  // the screen object (for destroy on switch)
static lv_obj_t *arc          = nullptr;
static lv_obj_t *soc_arc      = nullptr;  // outer ring: vehicle state of charge
static lv_obj_t *big_value    = nullptr;  // kW number, centre of ring (charging only)
static lv_obj_t *big_unit     = nullptr;  // "kW" (charging only)
static lv_obj_t *center_state = nullptr;  // state word in ring centre (not charging)
static lv_obj_t *pilot_lbl    = nullptr;  // pilot current + its source, below the rings
static lv_obj_t *soc_lbl      = nullptr;  // "78%  ·  210 km", below pilot
static lv_obj_t *datetime_lbl = nullptr;
static lv_obj_t *chip_row     = nullptr;  // flex row holding the status chips
static lv_obj_t *chip_temp    = nullptr;
static lv_obj_t *chip_wifi    = nullptr;
static lv_obj_t *chip_car     = nullptr;
static lv_obj_t *hostip_lbl   = nullptr;  // address, only when show_hostip
static lv_obj_t *msg_lbl      = nullptr;  // transient message (top strip, line 2)
static lv_obj_t *tile_title[3] = {nullptr, nullptr, nullptr};
static lv_obj_t *tile_value[3] = {nullptr, nullptr, nullptr};

// Which tile set is currently captioned, so the captions are rewritten on the
// idle <-> session transition rather than every second. -1 = not yet written.
static int captioned_session = -1;

// One stat tile: a rounded card with a dim caption and one big value.
static void make_tile(lv_obj_t *parent, int idx, lv_coord_t y)
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
  lv_label_set_text(t, "");
  lv_obj_set_style_text_color(t, COL_DIM, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  tile_title[idx] = t;

  // Fixed width + left align: the value changes every second and proportional
  // digits would otherwise shuffle the whole string sideways as it redraws.
  lv_obj_t *v = lv_label_create(tile);
  lv_label_set_text(v, "--");
  lv_obj_set_style_text_color(v, COL_TEXT, 0);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_36, 0);
  lv_obj_set_width(v, TILE_W - 16);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  tile_value[idx] = v;
}

// A status pill in the top-right row. Background carries the semantics; the row
// is a flex container so chips can be shown/hidden without manual re-layout.
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

void charge_screen_build()
{
  // Own LVGL screen, loaded now (the previous screen is on a separate object that
  // the caller deletes after this returns). If a charge screen already exists
  // (theme rebuild), keep it until the new one is loaded, then delete it — never
  // delete the *active* screen first, or LVGL's active-screen pointer dangles.
  lv_obj_t *old = charge_scr;
  lv_obj_t *scr = lv_obj_create(NULL);
  charge_scr = scr;
  lv_scr_load(scr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  captioned_session = -1;  // force the tile captions to be written on first update

  // --- Top strip, line 1: clock (left) + status chips (right) ---
  // Fixed width so the clock does not shuffle as digit widths change.
  datetime_lbl = lv_label_create(scr);
  lv_label_set_text(datetime_lbl, "");
  lv_obj_set_style_text_color(datetime_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(datetime_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_width(datetime_lbl, 220);
  lv_obj_set_style_text_align(datetime_lbl, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(datetime_lbl, LV_ALIGN_TOP_LEFT, 12, 6);

  chip_row = lv_obj_create(scr);
  lv_obj_remove_style_all(chip_row);
  lv_obj_set_size(chip_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(chip_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(chip_row, 6, 0);
  lv_obj_clear_flag(chip_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(chip_row, LV_ALIGN_TOP_RIGHT, -12, 4);
  chip_temp = make_chip(chip_row);
  chip_wifi = make_chip(chip_row);
  chip_car  = make_chip(chip_row);

  // --- Top strip, line 2: hostname / IP, or a transient message over the top ---
  hostip_lbl = lv_label_create(scr);
  lv_label_set_text(hostip_lbl, "");
  lv_obj_set_style_text_color(hostip_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(hostip_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(hostip_lbl, LV_ALIGN_TOP_LEFT, 12, 34);

  msg_lbl = lv_label_create(scr);
  lv_label_set_text(msg_lbl, "");
  lv_obj_set_style_text_color(msg_lbl, COL_ACCENT, 0);
  lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_18, 0);
  lv_obj_align(msg_lbl, LV_ALIGN_TOP_LEFT, 12, 31);
  lv_obj_add_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);

  // --- SoC ring (outer) — created first so the power ring sits on top ---
  soc_arc = lv_arc_create(scr);
  lv_obj_set_size(soc_arc, SOC_ARC_SIZE, SOC_ARC_SIZE);
  lv_obj_align(soc_arc, LV_ALIGN_LEFT_MID, SOC_ARC_LEFT, RING_Y);
  lv_arc_set_rotation(soc_arc, 135);
  lv_arc_set_bg_angles(soc_arc, 0, 270);
  lv_arc_set_range(soc_arc, 0, 100);
  lv_arc_set_value(soc_arc, 0);
  lv_obj_remove_style(soc_arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(soc_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(soc_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(soc_arc, SOC_BAND, LV_PART_MAIN);
  lv_obj_set_style_arc_width(soc_arc, SOC_BAND, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(soc_arc, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(soc_arc, COL_SOC, LV_PART_INDICATOR);
  lv_obj_add_flag(soc_arc, LV_OBJ_FLAG_HIDDEN);  // shown only with vehicle data

  // --- Power ring (inner) ---
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, RING_SIZE, RING_SIZE);
  lv_obj_align(arc, LV_ALIGN_LEFT_MID, RING_LEFT, RING_Y);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, RING_BAND, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, RING_BAND, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COL_ACCENT, LV_PART_INDICATOR);

  // --- Ring centre: state word (idle) OR big kW value (charging) ---
  center_state = lv_label_create(scr);
  lv_label_set_long_mode(center_state, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(center_state, STATE_WORD_FIT_W);
  lv_obj_set_style_text_align(center_state, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(center_state, "");
  lv_obj_set_style_text_color(center_state, COL_ACCENT, 0);
  lv_obj_set_style_text_font(center_state, &lv_font_montserrat_28, 0);
  lv_obj_align_to(center_state, arc, LV_ALIGN_CENTER, 0, 0);

  // Fixed width + centred: the kW value re-renders every second, and a
  // width-driven re-centre is exactly the jitter this is meant to avoid.
  big_value = lv_label_create(scr);
  lv_label_set_text(big_value, "0.00");
  lv_obj_set_style_text_color(big_value, COL_TEXT, 0);
  lv_obj_set_style_text_font(big_value, &lv_font_montserrat_48, 0);
  lv_obj_set_width(big_value, 140);
  lv_obj_set_style_text_align(big_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(big_value, arc, LV_ALIGN_CENTER, 0, -20);
  lv_obj_add_flag(big_value, LV_OBJ_FLAG_HIDDEN);

  big_unit = lv_label_create(scr);
  lv_label_set_text(big_unit, "kW");
  lv_obj_set_style_text_color(big_unit, COL_DIM, 0);
  lv_obj_set_style_text_font(big_unit, &lv_font_montserrat_20, 0);
  lv_obj_align_to(big_unit, big_value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
  lv_obj_add_flag(big_unit, LV_OBJ_FLAG_HIDDEN);

  // Pilot / allowed charge current and what set it, below the rings.
  pilot_lbl = lv_label_create(scr);
  lv_label_set_text(pilot_lbl, "");
  lv_obj_set_style_text_color(pilot_lbl, COL_TEXT, 0);
  // 18px, not 20: the widest source string ("48 A * demand shaper") renders
  // ~236px at 20 and touches both edges of the column, and a wrap would land on
  // top of the SoC readout below it.
  lv_obj_set_style_text_font(pilot_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_width(pilot_lbl, LEFT_COL_W);
  lv_obj_set_style_text_align(pilot_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(pilot_lbl, 0, 262);

  // Vehicle SoC / range readout, keyed by colour to the outer ring above it.
  soc_lbl = lv_label_create(scr);
  lv_label_set_text(soc_lbl, "");
  lv_obj_set_style_text_color(soc_lbl, COL_SOC, 0);
  lv_obj_set_style_text_font(soc_lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_width(soc_lbl, LEFT_COL_W);
  lv_obj_set_style_text_align(soc_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(soc_lbl, 0, 290);
  lv_obj_add_flag(soc_lbl, LV_OBJ_FLAG_HIDDEN);

  // --- Stat tiles (right column) ---
  make_tile(scr, 0, TILE_Y0);
  make_tile(scr, 1, TILE_Y1);
  make_tile(scr, 2, TILE_Y2);

  // New screen is now active; safe to delete the previous instance (theme rebuild).
  if(old) {
    lv_obj_del(old);
  }
}

// --- Power-ring tween -------------------------------------------------------
// lv_arc_set_value() snaps. At a 1 Hz sample rate that reads as a twitch rather
// than a gauge, so drive it through an animation instead. Only the band's dirty
// rect redraws, so the cost is small — but the caller has to pump
// lv_timer_handler() faster than 1 Hz while it runs (charge_screen_animating()).

static void arc_anim_exec(void *obj, int32_t v)
{
  lv_arc_set_value((lv_obj_t *)obj, (int16_t)v);
}

static void arc_set_animated(lv_obj_t *a, int target)
{
  // Cancel any tween still in flight BEFORE reading the arc's value. Mid-tween
  // that value is just wherever the animation has got to, so comparing against
  // it and returning early leaves the old animation running on to its ORIGINAL
  // target. That is how the ring ended up stuck at the last charging current
  // after a session stopped: the new (lower) target briefly matched the value
  // the outgoing tween was passing through, so the update was skipped.
  lv_anim_del(a, arc_anim_exec);

  int16_t cur = lv_arc_get_value(a);
  if (cur == target) {
    return;
  }
  lv_anim_t an;
  lv_anim_init(&an);
  lv_anim_set_var(&an, a);
  lv_anim_set_exec_cb(&an, arc_anim_exec);
  lv_anim_set_values(&an, cur, target);
  lv_anim_set_time(&an, ARC_ANIM_MS);
  lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
  lv_anim_start(&an);
}

bool charge_screen_animating()
{
  return lv_anim_count_running() > 0;
}

// Caption the tile column for the current mode. Called on the transition only.
static void set_tile_captions(bool session_active)
{
  if (session_active) {
    lv_label_set_text(tile_title[0], "ELAPSED");
    lv_label_set_text(tile_title[1], "DELIVERED");
    lv_label_set_text(tile_title[2], "CURRENT");
  } else {
    lv_label_set_text(tile_title[0], "TODAY");
    lv_label_set_text(tile_title[1], "THIS WEEK");
    lv_label_set_text(tile_title[2], "LIFETIME");
  }
}

// kWh at a precision that fits the tile at 36px.
static void format_kwh(char *buf, size_t len, double kwh)
{
  if (kwh >= 1000.0)     snprintf(buf, len, "%.0f kWh", kwh);
  else if (kwh >= 100.0) snprintf(buf, len, "%.1f kWh", kwh);
  else                   snprintf(buf, len, "%.2f kWh", kwh);
}

void charge_screen_update(const ChargeScreenData &d)
{
  char buf[64];

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
  } else {
    lv_obj_add_flag(big_value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(big_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(center_state, LV_OBJ_FLAG_HIDDEN);

    // Length-adaptive font: keep the large size for words that render inside the
    // ring, drop one size for the wide ones so they stay on a single line.
    lv_point_t sz;
    lv_txt_get_size(&sz, word, &lv_font_montserrat_28, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const lv_font_t *font = (sz.x <= STATE_WORD_FIT_W) ? &lv_font_montserrat_28
                                                       : &lv_font_montserrat_20;
    lv_obj_set_style_text_font(center_state, font, 0);

    lv_label_set_text(center_state, word);
    lv_obj_set_style_text_color(center_state, accent, 0);
    lv_obj_align_to(center_state, arc, LV_ALIGN_CENTER, 0, 0);
  }

  // Ring: current actually being delivered, as % of full scale.
  //
  // It used to fall back to the pilot setpoint when not charging, but the pilot
  // holds its last commanded value after a session ends, so an idle ring sat at
  // exactly the level it had been charging at -- indistinguishable from a
  // session still running. The ring is a flow gauge; when nothing is flowing it
  // should be empty. The pilot is still spelled out in text below it.
  // Full scale is the configured max current, so a full ring means "at the limit
  // you set" rather than some fixed number the user never chose: an 80 A unit
  // configured to 47 A would otherwise never take the ring past ~59%.
  float full_scale = (d.max_a > 0) ? (float)d.max_a : RING_FULL_SCALE_A;
  int ring = (int)(d.amps / full_scale * 100.0f);
  if (ring < 0) ring = 0; else if (ring > 100) ring = 100;
  arc_set_animated(arc, ring);

  // Outer ring + readout: vehicle state of charge, when we have it.
  if (d.soc_valid) {
    int soc = d.soc_percent;
    if (soc < 0) soc = 0; else if (soc > 100) soc = 100;
    lv_obj_clear_flag(soc_arc, LV_OBJ_FLAG_HIDDEN);
    arc_set_animated(soc_arc, soc);

    if (d.range_valid) {
      snprintf(buf, sizeof(buf), "%d%%  " LV_SYMBOL_BULLET "  %d %s", soc, d.range,
               d.range_miles ? "mi" : "km");
    } else {
      snprintf(buf, sizeof(buf), "%d%%", soc);
    }
    lv_label_set_text(soc_lbl, buf);
    lv_obj_clear_flag(soc_lbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(soc_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(soc_lbl, LV_OBJ_FLAG_HIDDEN);
  }

  // Pilot / allowed current, and which claim set it. Without the source this
  // number is a mystery whenever divert or a shaper is moving it around.
  if (d.pilot_source && d.pilot_source[0]) {
    snprintf(buf, sizeof(buf), "%d A  " LV_SYMBOL_BULLET "  %s", d.pilot_a, d.pilot_source);
  } else {
    snprintf(buf, sizeof(buf), "%d A pilot", d.pilot_a);
  }
  lv_label_set_text(pilot_lbl, buf);

  // Top strip line 1: date/time.
  lv_label_set_text(datetime_lbl, d.datetime ? d.datetime : "");

  // Status chips (top-right). Background carries the state, so the row is
  // readable as colour before it is readable as text.
  if (d.temp_valid) {
    // fmt_temp pads two trailing spaces for the old top-strip layout; a pill
    // sizes to its text, so trim them.
    int tn = fmt_temp(buf, sizeof(buf), d.temp_c, d.temp_fahrenheit);
    while (tn > 0 && buf[tn - 1] == ' ') buf[--tn] = '\0';
    // Warm but fine below 50 C; above that the temp throttle is in play.
    // Colour off the user's own throttle setpoint: amber approaching it, fault
    // colour once the throttle is actually holding current down. The active
    // claim is the truth for that -- inferring it from temperature alone would
    // light up during the recovery ramp, when it is no longer limiting.
    if (d.temp_throttling) {
      chip_set(chip_temp, buf, COL_FAULT, COL_BG);
    } else if (d.temp_throttle_setpoint > 0 &&
               d.temp_c >= (float)d.temp_throttle_setpoint - TEMP_WARN_MARGIN_C) {
      chip_set(chip_temp, buf, COL_WARN, COL_BG);
    } else {
      // No setpoint means throttling is off: there is no threshold the user
      // has asked about, so the chip stays neutral however warm it reads.
      chip_set(chip_temp, buf, COL_CARD, COL_TEXT);
    }
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

  if (d.vehicle_connected) {
    chip_set(chip_car, LV_SYMBOL_CHARGE, d.charging ? COL_OK : COL_CARD,
             d.charging ? COL_BG : COL_TEXT);
  } else {
    lv_obj_add_flag(chip_car, LV_OBJ_FLAG_HIDDEN);
  }

  // Tiles: session figures while plugged in, lifetime totals when idle.
  if (captioned_session != (int)d.session_active) {
    set_tile_captions(d.session_active);
    captioned_session = (int)d.session_active;
  }

  if (d.session_active) {
    uint32_t h = d.elapsed_s / 3600, m = (d.elapsed_s % 3600) / 60, s = d.elapsed_s % 60;
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    lv_label_set_text(tile_value[0], buf);

    if (d.session_wh >= 1000.0) snprintf(buf, sizeof(buf), "%.2f kWh", d.session_wh / 1000.0);
    else                        snprintf(buf, sizeof(buf), "%.0f Wh", d.session_wh);
    lv_label_set_text(tile_value[1], buf);

    // Amps is the number that matters here; volts rides along in the caption so
    // the tile keeps to one big figure.
    snprintf(buf, sizeof(buf), "%.1f A", d.amps);
    lv_label_set_text(tile_value[2], buf);
    snprintf(buf, sizeof(buf), "CURRENT  @ %.0f V", d.volts);
    lv_label_set_text(tile_title[2], buf);
  } else {
    format_kwh(buf, sizeof(buf), d.total_day_kwh);
    lv_label_set_text(tile_value[0], buf);
    format_kwh(buf, sizeof(buf), d.total_week_kwh);
    lv_label_set_text(tile_value[1], buf);
    format_kwh(buf, sizeof(buf), d.total_kwh);
    lv_label_set_text(tile_value[2], buf);
  }

  // Top strip line 2 belongs to transient messages. The address is reference
  // information, not status, so it lives on the standby screen instead -- unless
  // standby can never appear, in which case it falls back to here.
  if (d.msg_line && d.msg_line[0]) {
    lv_label_set_text(msg_lbl, d.msg_line);
    lv_obj_clear_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hostip_lbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(msg_lbl, LV_OBJ_FLAG_HIDDEN);
    if (d.show_hostip) {
      snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_BULLET "  %s",
               d.hostname ? d.hostname : "", d.ip ? d.ip : "");
      lv_label_set_text(hostip_lbl, buf);
      lv_obj_clear_flag(hostip_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(hostip_lbl, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void charge_screen_destroy()
{
  if (charge_scr) {
    // Belt and braces: lv_obj_del() already drops animations bound to the
    // objects it frees, but the tweens hold raw pointers into this tree and a
    // survivor would scribble on freed memory.
    lv_anim_del(arc, arc_anim_exec);
    lv_anim_del(soc_arc, arc_anim_exec);
    lv_obj_del(charge_scr);
    charge_scr = nullptr;
    arc = soc_arc = big_value = big_unit = center_state = nullptr;
    pilot_lbl = soc_lbl = datetime_lbl = msg_lbl = hostip_lbl = nullptr;
    chip_row = chip_temp = chip_wifi = chip_car = nullptr;
    for (int i = 0; i < 3; i++) {
      tile_title[i] = tile_value[i] = nullptr;
    }
    captioned_session = -1;
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
