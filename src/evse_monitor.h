#ifndef _OPENEVSE_EVSE_MONITOR_H
#define _OPENEVSE_EVSE_MONITOR_H

#include <Arduino.h>
#include <openevse.h>
#include <MicroTasks.h>

#ifdef ENABLE_MCP9808
#include <Wire.h>
#include <Adafruit_MCP9808.h>
#endif

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
        uint32_t _ready;
      public:
        DataReady();

        void ready(uint32_t data);
    };

    OpenEVSEClass &_openevse;

    EvseStateEvent _state;            // OpenEVSE State
    double _amp;                      // OpenEVSE Current Sensor
    double _voltage;                  // Voltage from OpenEVSE or MQTT
    double  _temp1;                    // Sensor DS3232 Ambient
    bool _temp1_valid;
    double _temp2;                    // Sensor MCP9808 Ambiet
    bool _temp2_valid;
    double _temp3;                    // Sensor TMP007 Infared
    bool _temp3_valid;
    double _temp4;                    // ESP32 WiFi board sensor
    bool _temp4_valid;
    double _temp_monitor;             // Derived temp value
    bool _temp_monitor_valid;
    long _pilot;                      // OpenEVSE Pilot Setting
    long _elapsed;                    // Elapsed time (only valid if charging)
    uint32_t _elapsed_set_time;

    double _session_wh;
    double _total_kwh;

    // Default OpenEVSE Fault Counters
    long _gfci_count;
    long _nognd_count;
    long _stuck_count;

    DataReady _data_ready;

    uint8_t _count;

#ifdef ENABLE_MCP9808
    Adafruit_MCP9808 _mcp9808;
#endif

    void updateFaultCounters(int ret, long gfci_count, long nognd_count, long stuck_count);
  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    EvseMonitor(OpenEVSEClass &openevse);
    ~EvseMonitor();

    bool begin();

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

    // Register for events
    void onStateChange(MicroTasks::EventListener *listner) {
      _state.Register(listner);
    }
    void onDataReady(MicroTasks::EventListener *listner) {
      _data_ready.Register(listner);
    }
};

#endif // _OPENEVSE_EVSE_MONITOR_H
