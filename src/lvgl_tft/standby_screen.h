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
