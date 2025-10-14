#ifndef __SCREEN_LOCK_H
#define __SCREEN_LOCK_H

#include "screen_base.h"

// Lock Screen - displayed when EVSE is disabled
class LockScreen : public ScreenBase
{
public:
  LockScreen(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);
  ~LockScreen() = default;

  void init() override;
  unsigned long update() override;

  // Set the lock message to display
  void setLockMessage(const char* message) { _lockMessage = message; }

private:
  const char* _lockMessage;
};

#endif // __SCREEN_LOCK_H