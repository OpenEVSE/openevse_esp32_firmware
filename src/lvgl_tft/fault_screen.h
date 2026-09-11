// src/lvgl_tft/fault_screen.h — full-screen fault detail for the stock TFT.
//
// Takes over from the charge screen for as long as the controller reports a
// fault. The charge screen names the fault in the ring; this one says what it
// means and what to do about it, because the person reading it is standing at a
// charger that has stopped and has no other source of advice. Read-only (no
// touch), so it cannot be dismissed -- it clears when the fault does.
//
// The copy itself lives in src/fault_text.h, free of LVGL so it can be unit
// tested on the host.
#ifndef __FAULT_SCREEN_H
#define __FAULT_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>

struct FaultScreenData {
  uint8_t  evse_state;      // OPENEVSE_STATE_*; selects the copy
  bool     wifi_client;     // true = STA, false = AP
  bool     wifi_connected;
  int      wifi_pct;        // STA signal %, smoothed/quantised by the caller
  int      sta_count;       // AP station count
  // No clock: the footer's whole job here is the address, and three items did
  // not fit the width without colliding. What someone needs from a stopped
  // charger is a way to reach it, not the time.
  const char *hostname;
  const char *ip;           // the way off this screen -- always shown here
};

void fault_screen_build();
void fault_screen_destroy();
// Requires a preceding fault_screen_build() (writes into its widgets).
void fault_screen_update(const FaultScreenData &d);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __FAULT_SCREEN_H
