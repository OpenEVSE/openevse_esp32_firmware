#ifndef _OPENEVSE_EVSE_MONITOR_H
#define _OPENEVSE_EVSE_MONITOR_H

#include <Arduino.h>
#include <openevse.h>
#include <MicroTasks.h>
#include "energy_meter.h"

#ifdef ENABLE_MCP9808
#include <Wire.h>
#include <Adafruit_MCP9808.h>
#endif

#ifndef EVSE_HEATBEAT_INTERVAL
#define EVSE_HEATBEAT_INTERVAL 5
#endif
#ifndef EVSE_HEARTBEAT_CURRENT
#define EVSE_HEARTBEAT_CURRENT 6
#endif

#define EVSE_MONITOR_TEMP_MONITOR       0
#define EVSE_MONITOR_TEMP_MAX           1
#define EVSE_MONITOR_TEMP_EVSE_DS3232   2
#define EVSE_MONITOR_TEMP_EVSE_MCP9808  3
#define EVSE_MONITOR_TEMP_EVSE_TMP007   4
#define EVSE_MONITOR_TEMP_ESP_MCP9808   5

#define EVSE_MONITOR_TEMP_COUNT         6

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
        bool isBootLocked() {
          return OPENEVSE_VFLAG_BOOT_LOCK == (getFlags() & OPENEVSE_VFLAG_BOOT_LOCK);
        }
    };

    class DataReady : public MicroTasks::Event
    {
      private:
        uint32_t _state;
        uint32_t _ready;
      public:
        DataReady(uint32_t ready);

        bool ready(uint32_t data);
    };

    class StateChangeEvent : public MicroTasks::Event
    {
      private:
        uint32_t _state;
        uint32_t _mask;
        uint32_t _trigger;
      public:
        StateChangeEvent(uint32_t mask, uint32_t trigger);

        bool update(uint32_t state);
    };

    class SettingsChangedEvent : public MicroTasks::Event
    {
      friend class EvseMonitor;
    };

    class Temperature
    {
      private:
        bool _valid;
        double _value;

      public:
        Temperature() : _valid(false), _value(0) { }

        void set(double value, bool valid = true) {
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
    double _power;                    // Calculated Power from _amp & _voltage & mono|threephase
    Temperature _temps[EVSE_MONITOR_TEMP_COUNT];
    EnergyMeter _energyMeter;

    // Default OpenEVSE Fault Counters
    long _gfci_count;
    long _nognd_count;
    long _stuck_count;

    // Current settings
    long _min_current;
    long _pilot;                      // OpenEVSE Pilot Setting
    long _max_configured_current;
    long _max_hardware_current;
    long _current_sensor_scale;
    long _current_sensor_offset;

    // Settings
    uint32_t _settings_flags;
    uint32_t _panic_temperature;
    uint32_t _heartbeat_interval;
    uint32_t _heartbeat_current;
    RapiSender *_sender;

    // Extended state (linco-work firmware)
    uint32_t _frequency;          // AC line frequency × 100 (from $GZ); 0 = unknown/unsupported
    bool _relay_dc1;              // DC relay 1 enabled (only valid when _relay_status_known)
    bool _relay_dc2;              // DC relay 2 enabled (only valid when _relay_status_known)
    bool _relay_ac;               // AC relay enabled (only valid when _relay_status_known)
    bool _relay_status_known;     // true once $GR has been answered by the controller
    char _chip_id[48];            // EVSE chip ID from $GI

    DataReady _data_ready;
    DataReady _boot_ready;
    StateChangeEvent _session_complete;

    uint32_t _count;
    bool _heartbeat;

    char _firmware_version[32];
    char _serial[16];

#ifdef ENABLE_MCP9808
    Adafruit_MCP9808 _mcp9808;
#endif

    SettingsChangedEvent _settings_changed; // Settings changed

    void updateFaultCounters(int ret, long gfci_count, long nognd_count, long stuck_count);

    void evseBoot(const char *firmware_version);
    void updateEvseState(uint8_t evse_state, uint8_t pilot_state, uint32_t vflags);
    void updateCurrentSettings(long min_current, long max_hardware_current, long pilot, long max_configured_current);

    void getStatusFromEvse(bool allowStart = true);
    void getChargeCurrentAndVoltageFromEvse();
    void getTemperatureFromEvse();
    void readFrequency();
    void readRelayStatus();
    void readChipId();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    enum class ServiceLevel:uint8_t {
      L1=1,
      L2=2,
      Auto=0
    };

    enum class LcdType:uint8_t {
      Mono,
      RGB
    };

    EvseMonitor(OpenEVSEClass &openevse);
    ~EvseMonitor();

    bool begin(RapiSender &sender);
    void unlock();
    void enable();
    void sleep();
    void disable();
    void restart();
    void setMaxConfiguredCurrent(long amps);
    void setMaxHardwareCurrent(long amps);

    void setPilot(long amps, bool force=false, std::function<void(int ret)> callback = NULL);
    void setVoltage(double volts, std::function<void(int ret)> callback = NULL);
    void setServiceLevel(ServiceLevel level, std::function<void(int ret)> callback = NULL);
    void configureCurrentSensorScale(long scale, long offset, std::function<void(int ret)> callback = NULL);
    void enableFeature(uint8_t feature, bool enabled, std::function<void(int ret)> callback = NULL);
    void enableDiodeCheck(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableGfiTestCheck(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableGroundCheck(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableStuckRelayCheck(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableVentRequired(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableTemperatureCheck(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableOvercurrentMonitor(bool enabled, std::function<void(int ret)> callback = NULL);
    void setPanicTemperature(uint32_t tempC, std::function<void(int ret)> callback = NULL);
    void enableFrontButton(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableBootLock(bool enabled, std::function<void(int ret)> callback = NULL);
    void enablePPAutoAmpacity(bool enabled, std::function<void(int ret)> callback = NULL);
    void enableZeroCrossSwitch(bool enabled, std::function<void(int ret)> callback = NULL);
    void setRelayEnable(int relay, bool enabled, std::function<void(int ret)> callback = NULL);
    void resetFaultCounters(std::function<void(int ret)> callback = NULL);
    void setHeartbeatSupervision(uint32_t interval, uint32_t current, std::function<void(int ret)> callback = NULL);
    void verifyPilot();

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
    bool isCharging() {
      return _state.isCharging();
    }
    bool isVehicleConnected() {
      return _state.isVehicleConnected();
    }
    bool isBootLocked() {
      return _state.isBootLocked();
    }
    double getAmps() {
      return _amp;
    }
    double getVoltage() {
      return _voltage;
    }
    double getPower() {
      return _power;
    }
    uint32_t getSessionElapsed() {
      return _energyMeter.getElapsed();
    }
    double getSessionEnergy() {
      return _energyMeter.getSession();
    }
    double getTotalEnergy() {
      return _energyMeter.getTotal();
    }
    double getTotalDay() {
      return _energyMeter.getDaily();
    }
    double getTotalWeek() {
      return _energyMeter.getWeekly();
    }
    double getTotalMonth() {
      return _energyMeter.getMonthly();
    }
    double getTotalYear() {
      return _energyMeter.getYearly();
    }
    bool saveEnergyMeter() {
      return _energyMeter.save();
    }
    bool resetEnergyMeter(bool full, bool import)
    {
      return _energyMeter.reset(full, import);
    }
    bool importTotalEnergy();
    void getAmmeterSettings();

    bool publishEnergyMeter() {
      return _energyMeter.publish();
    }
    void clearEnergyMeterSession() {
      _energyMeter.clearSession();
    }
    void createEnergyMeterJsonDoc(JsonDocument &doc) {
      _energyMeter.createEnergyMeterJsonDoc(doc);
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
    double getTemperature(uint8_t sensor) {
      return _temps[sensor].get();
    }
    double isTemperatureValid(uint8_t sensor) {
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
    long getCurrentSensorScale() {
      return _current_sensor_scale;
    }
    long getCurrentSensorOffset() {
      return _current_sensor_offset;
    }
    uint32_t getSettingsFlags() {
      return _settings_flags;
    }
    ServiceLevel getServiceLevel();
    ServiceLevel getActualServiceLevel();
    bool isDiodeCheckEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_DIODE_CHK_DISABLED);
    }
    bool isVentRequiredEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_VENT_REQ_DISABLED);
    }
    bool isGroundCheckEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_GND_CHK_DISABLED);
    }
    bool isStuckRelayCheckEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_STUCK_RELAY_CHK_DISABLED);
    }
    bool isGfiTestEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_GFI_TEST_DISABLED);
    }
    bool isTemperatureCheckEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_TEMP_CHK_DISABLED);
    }
    bool isOvercurrentMonitorEnabled() {
      // NB: the controller aliases this to the temp-check bit (both 0x0400),
      // so overcurrent and temperature monitoring cannot be toggled separately
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_OVERCURRENT_DISABLED);
    }
    uint32_t getPanicTemperature() { return _panic_temperature; }
    bool isFrontButtonEnabled() { return !isButtonDisabled(); }
    bool isButtonDisabled() {
      return OPENEVSE_ECF_BUTTON_DISABLED == (getSettingsFlags() & OPENEVSE_ECF_BUTTON_DISABLED);
    }
    bool isBootLockEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_BOOT_LOCK_DISABLED);
    }
    bool isPPAutoAmpacityEnabled() {
      return OPENEVSE_ECF_PP_AUTO_AMPACITY == (getSettingsFlags() & OPENEVSE_ECF_PP_AUTO_AMPACITY);
    }
    bool isZeroCrossSwitchEnabled() {
      return 0 == (getSettingsFlags() & OPENEVSE_ECF_RELAY_ZC_DISABLED);
    }
    bool isDC1RelayEnabled() { return _relay_dc1; }
    bool isDC2RelayEnabled() { return _relay_dc2; }
    bool isACRelayEnabled()  { return _relay_ac; }
    bool isRelayStatusKnown() { return _relay_status_known; }
    uint32_t getFrequency()  { return _frequency; }  // × 100 Hz (5000 = 50.00 Hz); 0 = unknown
    const char *getChipId()  { return _chip_id; }
    // True if the controller's RAPI protocol supports the D9 command set
    bool isD9Supported() { return _openevse.isD9Supported(); }
    uint32_t getHeartbeatInterval() { return _heartbeat_interval; }
    uint32_t getHeartbeatCurrent() { return _heartbeat_current; }
    bool isHeartbeatEnabled() { return _heartbeat_current > 0; }
    bool isAutoStartDisabled() {
      return OPENEVSE_ECF_AUTO_START_DISABLED == (getSettingsFlags() & OPENEVSE_ECF_AUTO_START_DISABLED);
    }
    bool isSerialDebugEnabled() {
      return OPENEVSE_ECF_SERIAL_DBG == (getSettingsFlags() & OPENEVSE_ECF_SERIAL_DBG);
    }
    LcdType getLcdType() {
      return (OPENEVSE_ECF_MONO_LCD == (getSettingsFlags() & OPENEVSE_ECF_MONO_LCD)) ?
        LcdType::Mono :
        LcdType::RGB;
    }
    const char *getFirmwareVersion() {
      return _firmware_version;
    }
    const char *getSerial() {
      return _serial;
    }

    // Register for events
    void onStateChange(MicroTasks::EventListener *listner) {
      _state.Register(listner);
    }
    void onSettingsChanged(MicroTasks::EventListener *listner) {
      _settings_changed.Register(listner);
    }
    void onDataReady(MicroTasks::EventListener *listner) {
      _data_ready.Register(listner);
    }
    void onBootReady(MicroTasks::EventListener *listner) {
      _boot_ready.Register(listner);
    }
    void onSessionComplete(MicroTasks::EventListener *listner) {
      _session_complete.Register(listner);
    }
  };

#endif // _OPENEVSE_EVSE_MONITOR_H
