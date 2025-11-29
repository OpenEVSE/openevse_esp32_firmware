#ifndef __SCREEN_BOOT_H
#define __SCREEN_BOOT_H

#include "screen_base.h"
#include "screen_renderer.h"

#define BOOT_PROGRESS_WIDTH     300
#define BOOT_PROGRESS_HEIGHT    16
#define BOOT_PROGRESS_X         ((TFT_SCREEN_WIDTH - BOOT_PROGRESS_WIDTH) / 2)
#define BOOT_PROGRESS_Y         195

class BootScreen : public ScreenBase
{
public:
  BootScreen(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) :
    ScreenBase(screen, evse, scheduler, manual),
    _boot_progress(0) {}

  void init() override {
    ScreenBase::init();
    _boot_progress = 0;
  }

  unsigned long update() override;

  // Checks if boot is complete
  bool isBootComplete() const { return _boot_progress >= 300; }

private:
  uint16_t _boot_progress;
};

#endif // __SCREEN_BOOT_H
