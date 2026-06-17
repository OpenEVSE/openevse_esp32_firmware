// src/lvgl_tft/charge_screen.h — the single LVGL status screen for the stock TFT.
// One widget tree built once; charge_screen_update() pushes a full snapshot each
// refresh (~1 Hz). Read-only (no touch). Same data as the original charge screen.
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
  float    power_kw;          // center value when charging
  int      pilot_a;           // center value when idle (charge-current setpoint)
  float    volts;
  float    amps;
  uint32_t elapsed_s;         // session elapsed
  double   session_wh;        // session energy delivered
  bool     temp_valid;
  float    temp_c;
  bool     wifi_client;       // true = STA, false = AP
  bool     wifi_connected;
  int      rssi;              // STA dBm (valid when wifi_client && connected)
  int      sta_count;         // AP connected stations (valid when !wifi_client)
  const char *datetime;       // "YYYY-MM-DD HH:MM:SS"
  const char *hostname;       // bottom-left
  const char *ip;             // bottom-right
  const char *msg_line;       // transient message (boot/OTA/status); "" when none — overrides host/ip
};

// Build + load the charge screen (own LVGL screen object).
void charge_screen_build();

// Delete the charge screen (call after another screen is loaded).
void charge_screen_destroy();

// Push a full snapshot. Cheap; only changed pixels re-flush (LVGL dirty-rect).
void charge_screen_update(const ChargeScreenData &d);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __CHARGE_SCREEN_H
