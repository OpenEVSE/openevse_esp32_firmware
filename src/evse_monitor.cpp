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

#ifndef EVSE_MONITOR_ENERGY_TIME
#define EVSE_MONITOR_ENERGY_TIME            2
#endif // !EVSE_MONITOR_ENERGY_TIME

#ifndef EVSE_HEATBEAT_INTERVAL
#define EVSE_HEATBEAT_INTERVAL              5
#endif
#ifndef EVSE_HEARTBEAT_CURRENT
#define EVSE_HEARTBEAT_CURRENT              6
#endif

#define EVSE_MONITOR_STATE_DATA_READY         (1 << 0)
#define EVSE_MONITOR_AMP_AND_VOLT_DATA_READY  (1 << 1)
#define EVSE_MONITOR_TEMP_DATA_READY          (1 << 2)
#define EVSE_MONITOR_ENERGY_DATA_READY        (1 << 3)

#define EVSE_MONITOR_DATA_READY (\
        EVSE_MONITOR_AMP_AND_VOLT_DATA_READY | \
        EVSE_MONITOR_TEMP_DATA_READY | \
        EVSE_MONITOR_ENERGY_DATA_READY \
)

#define EVSE_MONITOR_FAULT_COUNT_BOOT_READY   (1 << 0)
#define EVSE_MONITOR_FLAGS_BOOT_READY         (1 << 1)
#define EVSE_MONITOR_CURRENT_BOOT_READY       (1 << 2)
#define EVSE_MONITOR_ENERGY_BOOT_READY        (1 << 3)

#define EVSE_MONITOR_BOOT_READY ( \
        EVSE_MONITOR_FAULT_COUNT_BOOT_READY | \
        EVSE_MONITOR_FLAGS_BOOT_READY | \
        EVSE_MONITOR_CURRENT_BOOT_READY | \
        EVSE_MONITOR_ENERGY_BOOT_READY \
)

#define EVSE_MONITOR_SESSION_COMPLETE_MASK      OPENEVSE_VFLAG_EV_CONNECTED
#define EVSE_MONITOR_SESSION_COMPLETE_TRIGGER   0

EvseMonitor::EvseStateEvent::EvseStateEvent() :
  MicroTasks::Event(),
  _evse_state(OPENEVSE_STATE_STARTING)
{
}

bool EvseMonitor::EvseStateEvent::setState(uint8_t evse_state, uint8_t pilot_state, uint32_t vflags)
{
  if(_evse_state != evse_state ||
     _pilot_state != pilot_state ||
     _vflags != vflags)
  {
    _evse_state = evse_state;
    _pilot_state = pilot_state;
    _vflags = vflags;

    Trigger();

    return true;
  }

  return false;
}

EvseMonitor::DataReady::DataReady(uint32_t ready) :
  MicroTasks::Event(),
  _state(0),
  _ready(ready)
{
}

bool EvseMonitor::DataReady::ready(uint32_t data)
{
  _state |= data;
  if(_ready == (_state & _ready))
  {
    Trigger();
    _state = 0;
    return true;
  }

  return false;
}

EvseMonitor::StateChangeEvent::StateChangeEvent(uint32_t mask, uint32_t trigger) :
  MicroTasks::Event(),
  _state(0),
  _mask(mask),
  _trigger(trigger)
{
}

bool EvseMonitor::StateChangeEvent::update(uint32_t data)
{
  // Only interested in the mask bits changing
  data &= _mask;
  if(_state != data)
  {
    _state = data;
    if(_trigger == _state)
    {
      Trigger();
      return true;
    }
  }

  return false;
}

EvseMonitor::EvseMonitor(OpenEVSEClass &openevse) :
  MicroTasks::Task(),
  _openevse(openevse),
  _state(),
  _amp(0),
  _voltage(DEFAULT_VOLTAGE),
  _elapsed(0),
  _elapsed_set_time(0),
  _temps(),
  _session_wh(0),
  _total_kwh(0),
  _gfci_count(0),
  _nognd_count(0),
  _stuck_count(0),
  _min_current(0),
  _pilot(0),
  _max_configured_current(0),
  _max_hardware_current(0),
  _data_ready(EVSE_MONITOR_DATA_READY),
  _boot_ready(EVSE_MONITOR_BOOT_READY),
  _session_complete(EVSE_MONITOR_SESSION_COMPLETE_MASK, EVSE_MONITOR_SESSION_COMPLETE_TRIGGER),
  _count(0),
  _heartbeat(false),
  _firmware_version("")
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
}

void EvseMonitor::evseBoot(const char *firmware)
{
  DBUGF("EVSE boot v%s", firmware);

  snprintf(_firmware_version, sizeof(_firmware_version), "%s", firmware);

  _openevse.getFaultCounters([this](int ret, long gfci_count, long nognd_count, long stuck_count)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      updateFaultCounters(ret, gfci_count, nognd_count, stuck_count);
      _boot_ready.ready(EVSE_MONITOR_FAULT_COUNT_BOOT_READY);
    }
  });

  _openevse.getSettings([this](int ret, long pilot, uint32_t flags)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      DBUGF("pilot = %ld, flags = %x", pilot, flags);
      _settings_flags = flags;
      _boot_ready.ready(EVSE_MONITOR_FLAGS_BOOT_READY);
    }
  });

  _openevse.getCurrentCapacity([this](int ret, long min_current, long max_hardware_current, long pilot, long max_configured_current)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      DBUGF("min_current = %ld, pilot = %ld, max_configured_current = %ld, max_hardware_current = %ld", min_current, pilot, max_configured_current, max_hardware_current);
      _min_current = min_current;
      _max_hardware_current = max_hardware_current;
      _pilot = pilot;
      _max_configured_current = max_configured_current;
      _boot_ready.ready(EVSE_MONITOR_CURRENT_BOOT_READY);
    }
  });

  _openevse.getEnergy([this](int ret, double session_wh, double total_kwh)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      DBUGF("session_wh = %.2f, total_kwh = %.2f", session_wh, total_kwh);
      _total_kwh = total_kwh;

      _boot_ready.ready(EVSE_MONITOR_ENERGY_BOOT_READY);
    }
  });

  _openevse.heartbeatEnable(EVSE_HEATBEAT_INTERVAL, EVSE_HEARTBEAT_CURRENT, [this](int ret, int interval, int current, int triggered) {
    _heartbeat = RAPI_RESPONSE_OK == ret;
  });
}

void EvseMonitor::evseStateChanged()
{
  if(isError()) {
    _openevse.getFaultCounters([this](int ret, long gfci_count, long nognd_count, long stuck_count) { updateFaultCounters(ret, gfci_count, nognd_count, stuck_count); });
  }
  if(!isCharging()) {
    _amp = 0;
  }
  _session_complete.update(getFlags());
}

unsigned long EvseMonitor::loop(MicroTasks::WakeReason reason)
{
  DBUG("EVSE monitor woke: ");
  DBUG(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");
  DBUG(", _count = ");
  DBUGLN(_count);

  if(_heartbeat)
  {
    _openevse.heartbeatPulse([] (int ret)
    {
      if(RAPI_RESPONSE_OK != ret) {
        DEBUG_PORT.println("Heartbeat failed");
      }
    });
  }

  // Get the EVSE state
  if(0 == _count % EVSE_MONITOR_STATE_TIME) {
    getStatusFromEvse();
  }

  if(0 == _count % EVSE_MONITOR_AMP_AND_VOLT_TIME) {
    getChargeCurrentAndVoltageFromEvse();
  }

  if(0 == _count % EVSE_MONITOR_TEMP_TIME) {
    getTemperatureFromEvse();
  }

  if(0 == _count % EVSE_MONITOR_ENERGY_TIME) {
    getEnergyFromEvse();
  }

  _count ++;

  return EVSE_MONITOR_POLL_TIME;
}

bool EvseMonitor::begin(RapiSender &sender)
{
  _openevse.begin(sender, [this](bool connected, const char *firmware, const char *protocol)
  {
    if(connected)
    {
      _openevse.onState([this](uint8_t evse_state, uint8_t pilot_state, uint32_t current_capacity, uint32_t vflags)
      {
        DBUGF("evse_state = %02x, pilot_state = %02x, current_capacity = %d, vflags = %08x", evse_state, pilot_state, current_capacity, vflags);
        if(_state.setState(evse_state, pilot_state, vflags)) {
          evseStateChanged();
        }
      });

      _openevse.onBoot([this](uint8_t post_code, const char *firmware) { evseBoot(firmware); });

      evseBoot(firmware);

      MicroTask.startTask(this);
    } else {
      DEBUG_PORT.println("OpenEVSE not responding or not connected");
    }
  });

  return true;
}

void EvseMonitor::updateFaultCounters(int ret, long gfci_count, long nognd_count, long stuck_count)
{
  if(RAPI_RESPONSE_OK == ret)
  {
    DBUGF("gfci_count = %ld, nognd_count = %ld, stuck_count = %ld", gfci_count, nognd_count, stuck_count);

    _gfci_count = gfci_count;
    _nognd_count = nognd_count;
    _stuck_count = stuck_count;
  }
}

EvseMonitor::ServiceLevel EvseMonitor::getServiceLevel()
{
  if(OPENEVSE_ECF_AUTO_SVC_LEVEL_DISABLED == (getSettingsFlags() & OPENEVSE_ECF_AUTO_SVC_LEVEL_DISABLED)) {
    return ServiceLevel::Auto;
  }

  return (OPENEVSE_ECF_L2 == (getSettingsFlags() & OPENEVSE_ECF_L2)) ?
    ServiceLevel::L2 :
    ServiceLevel::L1;
}

void EvseMonitor::enable()
{
  OpenEVSE.enable([this](int ret)
  {
    DBUGF("EVSE: enable - complete %d", ret);
    if(RAPI_RESPONSE_OK == ret) {
      getStatusFromEvse();
    }
  });
}

void EvseMonitor::sleep()
{
  OpenEVSE.sleep([this](int ret)
  {
    DBUGF("EVSE: sleep - complete %d", ret);
    if(RAPI_RESPONSE_OK == ret) {
      getStatusFromEvse();
    }
  });
}

void EvseMonitor::disable()
{
  OpenEVSE.disable([this](int ret)
  {
    DBUGF("EVSE: disable - complete %d", ret);
    if(RAPI_RESPONSE_OK == ret) {
      getStatusFromEvse();
    }
  });
}

void EvseMonitor::setPilot(long amps)
{
  _openevse.setCurrentCapacity(amps, false, [this](int ret, long pilot)
  {
    if(RAPI_RESPONSE_OK == ret || RAPI_RESPONSE_NK == ret) {
      _pilot = pilot;
    }
  });
}

void EvseMonitor::getStatusFromEvse()
{
  DBUGLN("Get EVSE status");
  _openevse.getStatus([this](int ret, uint8_t evse_state, uint32_t session_time, uint8_t pilot_state, uint32_t vflags)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      DBUGF("evse_state = %02x, session_time = %d, pilot_state = %02x, vflags = %08x", evse_state, session_time, pilot_state, vflags);
      if(_state.setState(evse_state, pilot_state, vflags)) {
        evseStateChanged();
      }

      _elapsed = session_time;
      _elapsed_set_time = millis();

      _data_ready.ready(EVSE_MONITOR_STATE_DATA_READY);
    }
  });
}

void EvseMonitor::getChargeCurrentAndVoltageFromEvse()
{
  if(_state.isCharging())
  {
    DBUGLN("Get charge current/voltage status");
    _openevse.getChargeCurrentAndVoltage([this](int ret, double a, double volts)
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
  } else {
    _data_ready.ready(EVSE_MONITOR_AMP_AND_VOLT_DATA_READY);
  }
}

void EvseMonitor::getTemperatureFromEvse()
{
  DBUGLN("Get tempurature status");
  _openevse.getTemperature([this](int ret, double t1, bool t1_valid, double t2, bool t2_valid, double t3, bool t3_valid)
  {
    if(RAPI_RESPONSE_OK == ret)
    {
      DBUGF("t1 = %.1f%s, t2 = %.1f%s, t3 = %.1f%s", t1, t1_valid ? "" : "*", t2, t2_valid ? "" : "*", t3, t3_valid ? "" : "*");
      _temps[EVSE_MONITOR_TEMP_EVSE_DS3232].set(t1, t1_valid);
      _temps[EVSE_MONITOR_TEMP_EVSE_MCP9808].set(t2, t2_valid);
      _temps[EVSE_MONITOR_TEMP_EVSE_TMP007].set(t3, t3_valid);
      #ifdef ENABLE_MCP9808
      {
        double mcp9808_temp = _mcp9808.readTempC();
        DBUGVAR(mcp9808_temp);
        _temps[EVSE_MONITOR_TEMP_ESP_MCP9808].set(mcp9808_temp, !isnan(mcp9808_temp));
      }
      #endif

      _temps[EVSE_MONITOR_TEMP_MONITOR].invalidate();
      for(int i = 1; i < EVSE_MONITOR_TEMP_COUNT; i++)
      {
        if(_temps[i].isValid()) {
          _temps[EVSE_MONITOR_TEMP_MONITOR].set(_temps[i].get(), _temps[i].isValid());
          break;
        }
      }
      _data_ready.ready(EVSE_MONITOR_TEMP_DATA_READY);
    }
  });
}

void EvseMonitor::getEnergyFromEvse()
{
  if(_state.isCharging())
  {
    DBUGLN("Get charge energy usage");
    _openevse.getEnergy([this](int ret, double session_wh, double total_kwh)
    {
      if(RAPI_RESPONSE_OK == ret)
      {
        DBUGF("session_wh = %.2f, total_kwh = %.2f", session_wh, total_kwh);
        _session_wh = session_wh;
        _total_kwh = total_kwh;

        _data_ready.ready(EVSE_MONITOR_ENERGY_DATA_READY);
      }
    });
  } else {
    _data_ready.ready(EVSE_MONITOR_ENERGY_DATA_READY);
  }
}

