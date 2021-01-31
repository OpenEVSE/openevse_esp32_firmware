#ifndef _OPENEVSE_EVSE_MONITOR_H
#define _OPENEVSE_EVSE_MONITOR_H

#include <Arduino.h>
#include <openevse.h>
#include <MicroTasks.h>

#ifdef ENABLE_MCP9808
#include <Wire.h>
#include <Adafruit_MCP9808.h>
#endif

#define EVSE_MONITOR_TEMP_MONITOR       0
#define EVSE_MONITOR_TEMP_EVSE_DS3232   1
#define EVSE_MONITOR_TEMP_EVSE_MCP9808  2
#define EVSE_MONITOR_TEMP_EVSE_TMP007   3
#define EVSE_MONITOR_TEMP_ESP_MCP9808   4

#define EVSE_MONITOR_TEMP_COUNT         5

class EvseMonitor : public MicroTasks::Task
{
  private:

    class EvseStateEvent : public MicroTasks::Event
    {
      private:
        uint8_t _evse_state;
        uint8_t _pilot_state;
        uint32_t _vflags;
      public:
        EvseStateEvent();

        bool setState(uint8_t evse_state, uint8_t pilot_state, uint32_t vflags);

        uint8_t getEvseState() {
          return _evse_state;
        }
        uint8_t getPilotState() {
          return _pilot_state;
        }
        uint32_t getFlags() {
          return _vflags;
        }

        bool isActive() {
          return OPENEVSE_STATE_NOT_CONNECTED <= _evse_state && _evse_state <= OPENEVSE_STATE_CHARGING;
        }
        bool isDisabled() {
          return OPENEVSE_STATE_SLEEPING <= _evse_state;
        }
        bool isCharging() {
          return OPENEVSE_STATE_CHARGING == _evse_state;
        }
        bool isError() {
          return OPENEVSE_STATE_VENT_REQUIRED <= _evse_state && _evse_state <= OPENEVSE_STATE_OVER_CURRENT;
        }
        bool isVehicleConnected() {
          return OPENEVSE_VFLAG_EV_CONNECTED == (getFlags() & OPENEVSE_VFLAG_EV_CONNECTED);
        }
    };

    class DataReady : public MicroTasks::Event
    {
      private:
        uint32_t _state;
        uint32_t _ready;
      public:
        DataReady(uint32_t ready);

        void ready(uint32_t data);
    };

    class Tempurature
    {
      private:
        bool _valid;
        double _value;

      public:
        Tempurature() : _valid(false), _value(0) { }

        void set(double value, bool valid) {
          _value = value;
          _valid = valid;
        }

        double get() {
          return _value;
        }

        bool isValid() {
          return _valid;
        }

        void invalidate() {
          _valid = false;
        }
    };

    OpenEVSEClass &_openevse;

    EvseStateEvent _state;            // OpenEVSE State
    double _amp;                      // OpenEVSE Current Sensor
    double _voltage;                  // Voltage from OpenEVSE or MQTT
    long _elapsed;                    // Elapsed time (only valid if charging)
    uint32_t _elapsed_set_time;

    Tempurature _temps[EVSE_MONITOR_TEMP_COUNT];

    double _session_wh;
    double _total_kwh;

    // Default OpenEVSE Fault Counters
    long _gfci_count;
    long _nognd_count;
    long _stuck_count;

    // Current settings
    long _min_current;
    long _pilot;                      // OpenEVSE Pilot Setting
    long _max_configured_current;
    long _max_hardware_current;

    // Settings
    uint32_t _settings_flags;

    DataReady _data_ready;
    DataReady _boot_ready;

    uint32_t _count;

    char _firmware_version[32];

#ifdef ENABLE_MCP9808
    Adafruit_MCP9808 _mcp9808;
#endif

    void updateFaultCounters(int ret, long gfci_count, long nognd_count, long stuck_count);

    void evseBoot(const char *firmware_version);
  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    enum class ServiceLevel:uint8_t {
      L1,
      L2,
      Auto
    };

    EvseMonitor(OpenEVSEClass &openevse);
    ~EvseMonitor();

    bool begin(RapiSender &sender);

    uint8_t getEvseState() {
      return _state.getEvseState();
    }
    uint8_t getPilotState() {
      return _state.getPilotState();
    }
    uint32_t getFlags() {
      return _state.getFlags();
    }
    bool isActive() {
      return _state.isActive();
    }
    bool isDisabled() {
      return _state.isDisabled();
    }
    bool isError() {
      return _state.isError();
    }
    bool isVehicleConnected() {
      return _state.isVehicleConnected();
    }
    double getAmps() {
      return _amp;
    }
    double getVoltage() {
      return _voltage;
    }
    uint32_t getSessionElapsed() {
      return _elapsed + (_state.isCharging() ? ((millis() - _elapsed_set_time) / 1000) : 0);
    }
    double getSessionEnergy() {
      return _session_wh;
    }
    double getTotalEnergy() {
      return _total_kwh;
    }
    long getFaultCountGFCI() {
      return _gfci_count;
    }
    long getFaultCountNoGround() {
      return _nognd_count;
    }
    long getFaultCountStuckRelay() {
      return _stuck_count;
    }
    double getTempurature(uint8_t sensor) {
      return _temps[sensor].get();
    }
    double isTempuratureValid(uint8_t sensor) {
      return _temps[sensor].isValid();
    }
    long getMinCurrent() {
      return _min_current;
    }
    long getPilot() {
      return _pilot;
    }
    long getMaxConfiguredCurrent() {
      return _max_configured_current;
    }
    long getMaxHardwareCurrent() {
      return _max_hardware_current;
    }

    // Register for events
    void onStateChange(MicroTasks::EventListener *listner) {
      _state.Register(listner);
    }
    void onDataReady(MicroTasks::EventListener *listner) {
      _data_ready.Register(listner);
    }
    void onBootReady(MicroTasks::EventListener *listner) {
      _boot_ready.Register(listner);
    }
};

#endif // _OPENEVSE_EVSE_MONITOR_H
