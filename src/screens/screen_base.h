#ifndef __SCREEN_BASE_H
#define __SCREEN_BASE_H

#include <TFT_eSPI.h>
#include <PNGdec.h>
#include "evse_man.h"
#include "scheduler.h"
#include "manual.h"

class ScreenBase
{
public:
  ScreenBase(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) :
    _screen(screen),
    _evse(evse),
    _scheduler(scheduler),
    _manual(manual),
    _full_update(true) {}

  virtual ~ScreenBase() = default;

  // Initialize the screen, called once when screen becomes active
  virtual void init() { _full_update = true; }

  // Update/render the screen, returns milliseconds until next update
  virtual unsigned long update() = 0;

  // Handle any user input events
  virtual void handleEvent(uint8_t event) {}

  // Check if screen needs full redraw
  bool needsFullUpdate() const { return _full_update; }

  // Reset the full update flag after rendering
  void clearFullUpdate() { _full_update = false; }

  // Force a full redraw on next update
  void setFullUpdate() { _full_update = true; }

protected:
  TFT_eSPI &_screen;
  EvseManager &_evse;
  Scheduler &_scheduler;
  ManualOverride &_manual;
  bool _full_update;
};

#endif // __SCREEN_BASE_H
