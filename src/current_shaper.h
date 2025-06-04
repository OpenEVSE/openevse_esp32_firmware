#ifndef _OPENEVSE_CUR_SHAPER_H
#define _OPENEVSE_CUR_SHAPER_H

// Time between loop polls
#ifndef EVSE_SHAPER_LOOP_TIME
#define EVSE_SHAPER_LOOP_TIME 2000
#endif

#ifndef EVSE_SHAPER_MIN_FILTER
#define EVSE_SHAPER_MIN_FILTER 10 // sec
#endif

#ifndef EVSE_SHAPER_HYSTERESIS
#define EVSE_SHAPER_HYSTERESIS 0.5 // A
#endif

#include "emonesp.h"
#include <MicroTasks.h>
#include "evse_man.h"
#include "mqtt.h"
#include "app_config.h"
#include "http_update.h"
#include "input.h"
#include "event.h"
#include "divert.h"
#include "input_filter.h"

class CurrentShaperTask: public MicroTasks::Task
{
  private:
    EvseManager *_evse;
    bool         _enabled;
    bool         _changed;
    int          _max_pwr;   // total current available from the grid
    int          _live_pwr;  // current available to EVSE
    double       _smoothed_live_pwr; // filtered live power for getting out of pause only
    uint8_t      _chg_cur;   // calculated charge current to claim
    double       _max_cur;   // shaper calculated max current
    uint32_t     _timer;
    uint32_t     _pause_timer;
    bool         _updated;
    InputFilter  _inputFilter;
    InputFilter  _outputFilter;

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    CurrentShaperTask();
    ~CurrentShaperTask();
    void begin(EvseManager &evse);
    void shapeCurrent();
    void setMaxPwr(int max_pwr);
    void setLivePwr(int live_pwr);
    void setState(bool state);
    bool getState();
    int getMaxPwr();
    int getLivePwr();
    double getMaxCur();
    bool isActive();
    bool isUpdated();

    void notifyConfigChanged(bool enabled, uint32_t max_pwr);
};

extern CurrentShaperTask shaper;

#endif // CURRENT_SHAPER
