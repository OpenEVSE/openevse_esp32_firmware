#ifndef __SCREEN_FACTORY_H
#define __SCREEN_FACTORY_H

#include "screen_base.h"
#include "screen_boot.h"
#include "screen_charge.h"

class ScreenFactory {
public:
  static ScreenBase* createScreen(int screenType, TFT_eSPI &screen, EvseManager *evse,
                                  Scheduler *scheduler, ManualOverride *manual) {
    switch (screenType) {
      case 0: // SCREEN_BOOT
        return new BootScreen(screen, evse, scheduler, manual);
      case 1: // SCREEN_CHARGE
        return new ChargeScreen(screen, evse, scheduler, manual);
      // Add additional screen types here
      default:
        return nullptr;
    }
  }
};

#endif // __SCREEN_FACTORY_H
