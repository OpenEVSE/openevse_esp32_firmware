#ifndef __SCREEN_CHARGE_H
#define __SCREEN_CHARGE_H

#include "screen_base.h"
#include "screen_renderer.h"

class ChargeScreen : public ScreenBase
{
public:
  ChargeScreen(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) :
    ScreenBase(screen, evse, scheduler, manual),
    _previous_evse_state(0),
    wifi_client(false),
    wifi_connected(false) {}

  void init() override;
  unsigned long update() override;
  bool setWifiMode(bool client, bool connected);

private:
  uint8_t _previous_evse_state;
  bool wifi_client;
  bool wifi_connected;
};

#endif // __SCREEN_CHARGE_H
