// src/lvgl_tft/standby_screen.h — dimmed idle screen for the stock TFT.
// Ring + status word (left), TODAY/TOTAL kWh stacked (right), clock + temp/wifi
// in the top corners, host/IP in the bottom corners. Read-only (no touch).
#ifndef __STANDBY_SCREEN_H
#define __STANDBY_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>

struct StandbyScreenData {
  uint8_t  evse_state;        // OPENEVSE_STATE_* (drives the ring word + colour)
  bool     temp_valid;
  float    temp_c;
  bool     wifi_client;       // true = STA, false = AP
  bool     wifi_connected;
  int      rssi;              // STA dBm
  int      sta_count;         // AP station count
  double   today_kwh;         // getTotalDay()  (kWh)
  double   total_kwh;         // getTotalEnergy() (kWh)
  const char *clock;          // "YYYY-MM-DD  HH:MM:SS" (matches the charge header)
  const char *hostname;       // bottom-left
  const char *ip;             // bottom-right
};

void standby_screen_build();
void standby_screen_destroy();
// Requires a preceding standby_screen_build() (writes into its widgets).
void standby_screen_update(const StandbyScreenData &d);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __STANDBY_SCREEN_H
