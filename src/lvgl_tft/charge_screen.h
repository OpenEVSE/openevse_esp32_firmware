// src/lvgl_tft/charge_screen.h — the single LVGL status screen for the stock TFT.
// One widget tree built once; charge_screen_update() pushes a full snapshot each
// refresh (~1 Hz). Read-only (no touch).
#ifndef __CHARGE_SCREEN_H
#define __CHARGE_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>

// A full snapshot of what the screen shows. Assembled by LcdTask from EvseManager
// + WiFi + clock, then handed to charge_screen_update().
struct ChargeScreenData {
  uint8_t  evse_state;        // OPENEVSE_STATE_* (openevse.h)
  bool     vehicle_connected;
  bool     charging;          // evse_state == OPENEVSE_STATE_CHARGING
  float    power_kw;          // centre value when charging
  int      pilot_a;           // charge-current setpoint, shown under the ring
  int      max_a;             // configured max charge current (the Charger settings
                              // slider, not the hardware ceiling) -- the ring's
                              // full scale. 0 = unknown, ring falls back to a
                              // fixed scale.
  const char *pilot_source;   // what set pilot_a ("solar divert", "temp limit",
                              // ...); "" when no claim is active, i.e. the
                              // configured default. Set by lcd_lvgl's pilot_source_name().
  float    volts;
  float    amps;
  uint32_t elapsed_s;         // session elapsed
  double   session_wh;        // session energy delivered
  bool     session_active;    // vehicle plugged in — selects whether the tile
                              // column shows session figures or lifetime totals
  double   total_day_kwh;     // idle-tile figures (EvseManager::getTotalDay etc.)
  double   total_week_kwh;
  double   total_kwh;
  bool     soc_valid;
  int      soc_percent;       // vehicle state of charge
  bool     range_valid;
  int      range;             // vehicle remaining range, in range_miles' unit
  bool     range_miles;       // true = mi, false = km
  bool     temp_valid;
  float    temp_c;
  bool     temp_fahrenheit;   // render temp_c in °F
  // Temperature warning thresholds, from the throttle config rather than a
  // number picked here: warn as the panel approaches the setpoint the user
  // actually set, and flag it differently once the throttle is really holding
  // current down. temp_throttle_setpoint is in degrees C regardless of the
  // unit the chip is rendered in. Setpoint 0 means throttling is disabled, and
  // the chip stays neutral -- there is no threshold the user has asked about.
  int      temp_throttle_setpoint;  // degrees C, 0 when throttling is disabled
  bool     temp_throttling;         // the throttle claim is active right now
  bool     wifi_client;       // true = STA, false = AP
  bool     wifi_connected;
  int      wifi_pct;          // STA signal %, already smoothed and quantised by
                              // the caller (valid when wifi_client && connected)
  int      sta_count;         // AP connected stations (valid when !wifi_client)
  const char *datetime;       // "YYYY-MM-DD  HH:MM"
  const char *hostname;       // only rendered when show_hostip
  const char *ip;             // only rendered when show_hostip
  bool     show_hostip;       // The address lives on the standby screen and this
                              // screen normally stays free of it. Set this when
                              // standby is unreachable (backlight timeout of 0)
                              // so the address isn't left nowhere at all.
  const char *msg_line;       // transient message (boot/OTA/status); "" when none
                              // — owns the top strip's second line
};

// Build + load the charge screen (own LVGL screen object).
void charge_screen_build();

// Delete the charge screen (call after another screen is loaded).
void charge_screen_destroy();

// Push a full snapshot. Cheap; only changed pixels re-flush (LVGL dirty-rect).
void charge_screen_update(const ChargeScreenData &d);

// True while an LVGL animation (the power-ring tween) is still running, so the
// caller can pump lv_timer_handler() faster than its normal 1 Hz data cadence.
bool charge_screen_animating();

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __CHARGE_SCREEN_H
