#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EVSE_MONITOR)
#undef ENABLE_DEBUG
#endif

#include <openevse.h>

#include "emonesp.h"
#include "evse_monitor.h"
#include "debug.h"

#ifdef ENABLE_MCP9808
#ifndef I2C_SDA
#define I2C_SDA -1
#endif
#ifndef I2C_SCL
#define I2C_SCL -1
#endif
#endif

// Time between loop polls
#ifndef EVSE_MONITOR_POLL_TIME
#define EVSE_MONITOR_POLL_TIME 1000
#endif // !EVSE_MONITOR_POLL_TIME

// These poll times are in terms of number of EVSE_MONITOR_POLL_TIME

#ifndef EVSE_MONITOR_STATE_TIME
#define EVSE_MONITOR_STATE_TIME             30
#endif // !EVSE_MONITOR_STATE_TIME

#ifndef EVSE_MONITOR_AMP_AND_VOLT_TIME
#define EVSE_MONITOR_AMP_AND_VOLT_TIME      1
#endif // !EVSE_MONITOR_AMP_AND_VOLT_TIME

#ifndef EVSE_MONITOR_TEMP_TIME
#define EVSE_MONITOR_TEMP_TIME              30
#endif // !EVSE_MONITOR_TEMP_TIME

#define EVSE_MONITOR_STATE_DATA_READY         (1 << 0)
#define EVSE_MONITOR_AMP_AND_VOLT_DATA_READY  (1 << 1)
#define EVSE_MONITOR_TEMP_DATA_READY          (1 << 2)

#define EVSE_MONITOR_DATA_READY (\
        EVSE_MONITOR_AMP_AND_VOLT_DATA_READY | \
        EVSE_MONITOR_TEMP_DATA_READY \
)

EvseMonitor::EvseStateEvent::EvseStateEvent() :
  MicroTasks::Event(),
  _evse_state(OPENEVSE_STATE_STARTING)
{
}

void EvseMonitor::EvseStateEvent::setState(uint8_t evse_state, uint8_t pilot_state, uint32_t vflags)
{
  if(_evse_state != evse_state ||
     _pilot_state != pilot_state ||
     _vflags != vflags)
  {
    _evse_state = evse_state;
    _pilot_state = pilot_state;
    _vflags = vflags;
    Trigger();
  }
}

EvseMonitor::DataReady::DataReady() :
  MicroTasks::Event(),
  _ready(0)
{
}

void EvseMonitor::DataReady::ready(uint32_t data)
{
  _ready |= data;
  if(EVSE_MONITOR_DATA_READY == _ready)
  {
    Trigger();
    _ready = 0;
  }
}

EvseMonitor::EvseMonitor(OpenEVSEClass &openevse) :
  MicroTasks::Task(),
  _openevse(openevse),
  _state(),
  _amp(0),
  _voltage(DEFAULT_VOLTAGE),
  _temp1(0),
  _temp1_valid(false),
  _temp2(0),
  _temp2_valid(false),
  _temp3(0),
  _temp3_valid(false),
  _temp4(0),
  _temp4_valid(false),
  _temp_monitor(0),
  _temp_monitor_valid(false),
  _pilot(0),
  _elapsed(0),
  _count(0)
#ifdef ENABLE_MCP9808
  , _mcp9808()
#endif
{
}

EvseMonitor::~EvseMonitor()
{
}

void EvseMonitor::setup()
{
  #ifdef ENABLE_MCP9808
  Wire.begin(I2C_SDA, I2C_SCL);

  if(_mcp9808.begin())
  {
    DBUGLN("Found MCP9808!");
    // Mode Resolution SampleTime
    //  0    0.5°C       30 ms
    //  1    0.25°C      65 ms
    //  2    0.125°C     130 ms
    //  3    0.0625°C    250 ms
    _mcp9808.setResolution(1); // sets the resolution mode of reading, the modes are defined in the table bellow:
    _mcp9808.wake();   // wake up, ready to read!
  } else {
    DBUGLN("Couldn't find MCP9808!");
  }
  #endif

  _openevse.onState([this](uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)
  {
    DBUGVAR(evse_state);
    DBUGVAR(pilot_state);
    DBUGVAR(current_capacity);
    DBUGVAR(vflags);
    _state.setState(evse_state, pilot_state, vflags);
  });
}

unsigned long EvseMonitor::loop(MicroTasks::WakeReason reason)
{
  DBUG("EVSE monitor woke: ");
  DBUG(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");
  DBUGLN();

  // Get the EVSE state
  if(0 == _count % EVSE_MONITOR_STATE_TIME)
  {
    DBUGLN("Get EVSE status");
    _openevse.getStatus([this](int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)
    {
      if(RAPI_RESPONSE_OK == ret)
      {
        DBUGF("evse_state = %02x, session_time = %d, pilot_state = %02x, vflags = %08x", evse_state, session_time, pilot_state, vflags);
        _state.setState(evse_state, pilot_state, vflags);
        _elapsed = session_time;
        _elapsed_set_time = millis();
        _data_ready.ready(EVSE_MONITOR_STATE_DATA_READY);
      }
    });
  }

  if(_state.isCharging() && 0 == _count % EVSE_MONITOR_AMP_AND_VOLT_TIME)
  {
    DBUGLN("Get charge current/voltage status");
    OpenEVSE.getChargeCurrentAndVoltage([this](int ret, double a, double volts)
    {
      if(RAPI_RESPONSE_OK == ret)
      {
        DBUGF("amps = %.2f, volts = %.2f", a, volts);
        _amp = a;
        if(volts >= 0) {
          _voltage = volts;
        }
        _data_ready.ready(EVSE_MONITOR_AMP_AND_VOLT_DATA_READY);
      }
    });
  }

  if(0 == _count % EVSE_MONITOR_TEMP_TIME)
  {
    DBUGLN("Get tempurature status");
    OpenEVSE.getTemperature([this](int ret, double t1, bool t1_valid, double t2, bool t2_valid, double t3, bool t3_valid)
    {
      if(RAPI_RESPONSE_OK == ret)
      {
        DBUGF("t1 = %.1f%s, t2 = %.1f%s, t3 = %.1f%s", t1, t1_valid ? "" : "*", t2, t2_valid ? "" : "*", t3, t3_valid ? "" : "*");
        _temp_monitor_valid = false;
        _temp1 = t1;
        _temp1_valid = t1_valid;
        if(!_temp_monitor_valid && _temp1_valid) {
          _temp_monitor = _temp1;
          _temp_monitor_valid = true;
        }
        _temp2 = t2;
        _temp2_valid = t2_valid;
        if(!_temp_monitor_valid && _temp2_valid) {
          _temp_monitor = _temp2;
          _temp_monitor_valid = true;
        }
        _temp3 = t3;
        _temp3_valid = t3_valid;
        #ifdef ENABLE_MCP9808
        _temp4 = _mcp9808.readTempC();
        DBUGVAR(_temp4);
        _temp4_valid = !isnan(_temp4);
        if(!_temp_monitor_valid && _temp4_valid) {
          _temp_monitor = _temp4;
          _temp_monitor_valid = true;
        }
        #endif
        _data_ready.ready(EVSE_MONITOR_TEMP_DATA_READY);
      }
    });
  }

  _count ++;

  return EVSE_MONITOR_POLL_TIME;
}

bool EvseMonitor::begin()
{
  MicroTask.startTask(this);


  return true;
}
