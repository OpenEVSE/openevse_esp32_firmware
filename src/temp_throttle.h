#ifndef _OPENEVSE_TEMP_THROTTLE_H
#define _OPENEVSE_TEMP_THROTTLE_H

#ifndef TEMP_THROTTLE_LOOP_TIME
#define TEMP_THROTTLE_LOOP_TIME 30000
#endif

#ifndef TEMP_THROTTLE_SETPOINT_DEFAULT
#define TEMP_THROTTLE_SETPOINT_DEFAULT 65
#endif

#ifndef TEMP_THROTTLE_SETPOINT_MIN
#define TEMP_THROTTLE_SETPOINT_MIN 40
#endif

#ifndef TEMP_THROTTLE_SETPOINT_MAX
#define TEMP_THROTTLE_SETPOINT_MAX 80
#endif

#include "emonesp.h"
#include <MicroTasks.h>
#include "evse_man.h"
#include "app_config.h"

class TempThrottleTask : public MicroTasks::Task
{
  private:
    EvseManager *_evse;
    bool         _enabled;
    uint32_t     _setpoint;
    uint32_t     _start_current;    // current captured when throttle first engaged; 0 = not throttling
    uint32_t     _throttled_current;

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    TempThrottleTask();
    ~TempThrottleTask();
    void begin(EvseManager &evse);
    bool isThrottling();
    uint32_t getThrottledCurrent();
    void notifyConfigChanged(bool enabled, uint32_t setpoint);
};

extern TempThrottleTask tempThrottle;

#endif // _OPENEVSE_TEMP_THROTTLE_H
