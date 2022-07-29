#ifndef _OPENEVSE_CUR_SHAPER_H
#define _OPENEVSE_CUR_SHAPER_H

// Time between loop polls
#ifndef EVSE_SHAPER_LOOP_TIME
#define EVSE_SHAPER_LOOP_TIME 2000
#endif 
#ifndef EVSE_SHAPER_FAILSAFE_TIME
#define EVSE_SHAPER_FAILSAFE_TIME 360000
#endif 

#include "emonesp.h"
#include <MicroTasks.h>
#include "evse_man.h"
#include "mqtt.h"
#include "app_config.h"
#include "http_update.h"
#include "input.h"
#include "event.h"

class CurrentShaperTask: public MicroTasks::Task
{
  private:
    EvseManager *_evse;
    bool         _enabled;
    bool         _changed;
    int          _max_pwr;   // total current available from the grid
    int          _live_pwr;   // current available to EVSE
    uint8_t      _chg_cur;   // calculated charge current to claim
    uint32_t     _timer;
  
  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);
    void shapeCurrent();


  public:
    CurrentShaperTask();
    ~CurrentShaperTask();
    void begin(EvseManager &evse);

    void setMaxPwr(int max_pwr);
    void setLivePwr(int live_pwr);
    void setState(bool state);
    bool getState();
    int getMaxPwr();
    int getLivePwr();
    uint8_t getChgCur();
    bool isActive();

    void notifyConfigChanged(bool enabled, uint32_t max_pwr);
};

extern CurrentShaperTask shaper;

#endif // CURRENT_SHAPER
