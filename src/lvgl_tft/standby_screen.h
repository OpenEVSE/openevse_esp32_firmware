// src/lvgl_tft/standby_screen.h — dimmed idle screen for the stock TFT.
// Deliberately the charge screen's layout with the live figures removed: same
// ring in the same place, same tile column, same top strip. It is meant to read
// as that screen turned down, not as a different one. Read-only (no touch).
#ifndef __STANDBY_SCREEN_H
#define __STANDBY_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>

struct StandbyScreenData {
  uint8_t  evse_state;        // OPENEVSE_STATE_* (drives the ring word + colour)
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
  int      wifi_pct;          // STA signal %, smoothed/quantised by the caller
  int      sta_count;         // AP station count
  double   today_kwh;         // getTotalDay()   (kWh)
  double   week_kwh;          // getTotalWeek()  (kWh)
  double   total_kwh;         // getTotalEnergy()(kWh)
  const char *clock;          // "YYYY-MM-DD  HH:MM" (matches the charge header)
  const char *hostname;       // top strip, second line
  const char *ip;             // top strip, second line
};

void standby_screen_build();
void standby_screen_destroy();
// Requires a preceding standby_screen_build() (writes into its widgets).
void standby_screen_update(const StandbyScreenData &d);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __STANDBY_SCREEN_H
