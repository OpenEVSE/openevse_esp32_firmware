#include "debug.h"
#include "legacy_support.h"

#define IMPORT_DAYS (SCHEDULER_DAY_SUNDAY | SCHEDULER_DAY_MONDAY | SCHEDULER_DAY_TUESDAY | SCHEDULER_DAY_WEDNESDAY | SCHEDULER_DAY_THURSDAY | SCHEDULER_DAY_FRIDAY | SCHEDULER_DAY_SATURDAY | SCHEDULER_REPEAT)

void import_timers(Scheduler *scheduler)
{
  DBUGLN("Checking for existing timers");
  OpenEVSE.getTimer([scheduler](int ret, int start_hour, int start_minute, int end_hour, int end_minute)
  {
    DBUGVAR(start_hour);
    DBUGVAR(start_minute);
    DBUGVAR(end_hour);
    DBUGVAR(end_minute);

    if(0 != start_hour || 0 != start_minute || 0 != end_hour || 0 != end_minute)
    {
      DBUGF("Found existing timer: %d, %d, %d, %d", start_hour, start_minute, end_hour, end_minute);
      scheduler->addEvent(1, start_hour, start_minute, 0, IMPORT_DAYS, EvseState(EvseState::Active));
      scheduler->addEvent(2, end_hour, end_minute, 0, IMPORT_DAYS, EvseState(EvseState::Disabled));
      OpenEVSE.clearTimer([](int ret) {});
    } else {
      DBUGLN("No timers on EVSE board");
    }
  });
}
