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

// How long a voltage received over MQTT is considered "available" before we
// fall back to the statically configured ($SV/$GV) voltage. Refreshed on every
// MQTT voltage message.
#ifndef EVSE_MONITOR_MQTT_VOLTAGE_TIMEOUT_MS
#define EVSE_MONITOR_MQTT_VOLTAGE_TIMEOUT_MS  120000UL
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
        // Listed explicitly rather than as a range: the controller's fault
        // states are not contiguous (0x0C/0x0D are reserved), so a range check
        // silently treats anything added above it as a non-error. Keep in sync
        // with the OPENEVSE_STATE_* fault values in the OpenEVSE library.
        bool isError() {
          switch(_evse_state)
          {
            case OPENEVSE_STATE_VENT_REQUIRED:
            case OPENEVSE_STATE_DIODE_CHECK_FAILED:
            case OPENEVSE_STATE_GFI_FAULT:
            case OPENEVSE_STATE_NO_EARTH_GROUND:
            case OPENEVSE_STATE_STUCK_RELAY:
            case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
            case OPENEVSE_STATE_OVER_TEMPERATURE:
            case OPENEVSE_STATE_OVER_CURRENT:
            case OPENEVSE_STATE_RELAY_CLOSURE_FAULT:
            case OPENEVSE_STATE_PP_SHORTED:
            case OPENEVSE_STATE_PP_MISSING:
            case OPENEVSE_STATE_EEPROM_FAILURE:
              return true;
            default:
              return false;
          }
        }
        bool isVehicleConnected() {
          // OPENEVSE_VFLAG_EV_CONNECTED is documented in the controller as
          // "valid only when pilot not N12", and J1772EVSEController::Disable()
          // sets exactly that -- so while the EVSE is DISABLED the controller
          // cannot see a plug or unplug and simply leaves the flag at whatever
          // it was when the pause started. Reporting that stale value showed a
          // vehicle still connected long after it had been unplugged.
          //
          // SLEEPING is deliberately not covered: it holds the pilot at P12 and
          // keeps detecting normally, so the flag stays trustworthy there.
          if(OPENEVSE_STATE_DISABLED == getEvseState()) {
            return false;
          }
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
    double _voltage;                  // Resolved voltage: MQTT > configured ($SV/$GV) > default
    double _mqtt_voltage;             // Last voltage received over MQTT (0 = none received)
    uint32_t _mqtt_voltage_time;      // millis() of last MQTT voltage (0 = never)
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
    // Relay-open current-zero threshold (mA), from $GZ's 2nd field: current
    // below which the controller considers it safe to open the relay at a
    // current zero. Runtime-configurable on the controller via $SZ.
    // OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE = unknown/unsupported controller.
    uint32_t _zero_cross_threshold_ma;
    bool _relay_dc1;              // DC relay 1 enabled (only valid when _relay_status_known)
    bool _relay_dc2;              // DC relay 2 enabled (only valid when _relay_status_known)
    bool _relay_ac;               // AC relay enabled (only valid when _relay_status_known)
    bool _relay_status_known;     // true once $GR has been answered by the controller
    char _chip_id[48];            // EVSE chip ID from $GI

    // Relay contact-life health estimate (linco-work RELAY_HEALTH feature,
    // from $GL). Only meaningful once _relay_health_known is true - the
    // controller may predate the feature or have it compiled out.
    bool _relay_health_known;
    uint8_t  _relay_life_remaining_pct;
    uint32_t _relay_cold_open_count;
    uint32_t _relay_elec_damage_x1e6;
    uint32_t _relay_transit_baseline_ms;   // OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE = baseline not established
    bool     _relay_transit_drift_warning;
    uint32_t _relay_thermal_index_x100;    // OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE = not available
    uint32_t _relay_thermal_baseline_x100; // OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE = not available
    uint8_t  _relay_thermal_warning_level; // 0=ok/not available 1=watch 2=warn
    uint32_t _relay_stuck_recovery_count;  // cumulative stuck-relay recovery attempts run; 0 against pre-9.3.0 controllers
    // True while a $FK command is outstanding (up to ~30s) - pauses loop()'s
    // own periodic RAPI traffic so it doesn't overflow the RAPI queue and
    // drop heartbeat pulses for that long. See runStuckRelayRecovery().
    bool _relay_recovery_in_flight;

#ifdef ENABLE_CABLE_TEMP
    // Cable NTC thermistor monitoring (linco-work CABLE_TEMPERATURE_MONITORING
    // feature, firmware 9.4.0+, from $GN/$SN). Four logical sources: EV1/EV2
    // for the EV cable, IN1/IN2 for the input cable. Only meaningful once
    // _cable_temp_known is true - the controller may predate the feature or
    // have it compiled out.
    bool _cable_temp_known;
    // Last enable/disable this session actually commanded and had the
    // controller accept ($FF C). The controller has no read-back for that
    // bit, so isCableTempEnabled() otherwise has to infer it from the
    // sources - which reads "off" the moment the feature is turned on but
    // before any source is assigned, since every source reports
    // NOT_INSTALLED either way. This short-circuits that false negative for
    // exactly the session that did the commanding; reset on boot since a
    // fresh controller's actual state is unknown until re-inferred.
    bool _cable_temp_commanded;
    // Live readings. The Temperature "valid" flag tracks _STATUS_OK only; the
    // parallel status array carries which of the three non-reading conditions
    // applies (unassigned / open circuit / shorted), which a bool cannot.
    Temperature _cable_temps[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
    uint8_t _cable_temp_status[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
    // Per-source configuration, cached from $GN idx. Refreshed on boot and
    // after any successful write, not polled - it only changes when something
    // writes it.
    bool _cable_temp_cfg_known;
    uint32_t _cable_temp_cfg_refresh;
    uint8_t _cable_temp_cfg_responses;
    bool _cable_temp_cfg_success;
    uint8_t  _cable_temp_pin[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
    uint32_t _cable_temp_r25[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
    uint32_t _cable_temp_beta[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
    int32_t  _cable_temp_offset_c10[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
    int32_t  _cable_temp_panic_c10[OPENEVSE_CABLE_TEMP_SOURCE_COUNT];
#endif // ENABLE_CABLE_TEMP

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
    void getSettingsFromEvse();
    void getChargeCurrentAndVoltageFromEvse();
    void updateEffectiveVoltage();
    void getTemperatureFromEvse();
    void readFrequency();
    void readRelayStatus();
    void readChipId();
    void readRelayHealth();
#ifdef ENABLE_CABLE_TEMP
    void readCableTemperatures();
    // All 4 sources, boot-time only (see the call site) - $GN idx x4.
    void readCableTempConfig();
    // One source, after a targeted write - $GN idx x1. Updates that source's
    // cache directly without touching _cable_temp_cfg_known: that flag is a
    // boot-time "have all 4 ever been read together" gate, and a single-
    // source refresh has no bearing on it either way.
    void readCableTempConfig(uint8_t source);
#endif // ENABLE_CABLE_TEMP

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
    void setMqttVoltage(double volts);
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
    // Relay-open current-zero threshold, mA. OPENEVSE_RELAY_HEALTH_NOT_AVAILABLE if unknown/unsupported
    uint32_t getZeroCrossThresholdMa() { return _zero_cross_threshold_ma; }
    const char *getChipId()  { return _chip_id; }

    // Relay contact-life health estimate (requires the controller's
    // RELAY_HEALTH feature; check isRelayHealthKnown() first)
    bool isRelayHealthKnown() { return _relay_health_known; }
    uint8_t getRelayLifeRemainingPct() { return _relay_life_remaining_pct; }
    uint32_t getRelayColdOpenCount() { return _relay_cold_open_count; }
    uint32_t getRelayElecDamageX1e6() { return _relay_elec_damage_x1e6; }
    uint32_t getRelayTransitBaselineMs() { return _relay_transit_baseline_ms; }
    bool isRelayTransitDriftWarning() { return _relay_transit_drift_warning; }
    uint32_t getRelayThermalIndexX100() { return _relay_thermal_index_x100; }
    uint32_t getRelayThermalBaselineX100() { return _relay_thermal_baseline_x100; }
    uint8_t getRelayThermalWarningLevel() { return _relay_thermal_warning_level; }
    uint32_t getRelayStuckRecoveryCount() { return _relay_stuck_recovery_count; }

#ifdef ENABLE_CABLE_TEMP
    // Cable NTC thermistor monitoring (requires the controller's
    // CABLE_TEMPERATURE_MONITORING feature; check isCableTempKnown() first).
    // source is an OPENEVSE_CABLE_TEMP_SOURCE_xxx index.
    bool isCableTempKnown() { return _cable_temp_known; }
    bool isCableTempConfigKnown() { return _cable_temp_cfg_known; }
    // True when this source produced an actual reading. False covers all three
    // non-reading conditions - use getCableTempStatus() to tell them apart.
    bool isCableTempValid(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT && _cable_temps[source].isValid();
    }
    double getCableTemp(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ? _cable_temps[source].get() : 0;
    }
    // OPENEVSE_CABLE_TEMP_STATUS_xxx: OK / NOT_INSTALLED / OPEN / SHORTED
    uint8_t getCableTempStatus(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ?
        _cable_temp_status[source] : OPENEVSE_CABLE_TEMP_STATUS_NOT_INSTALLED;
    }
    // True if this source is wired to an input, i.e. it is actually in use.
    bool isCableTempAssigned(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT &&
             OPENEVSE_CABLE_TEMP_PIN_NONE != _cable_temp_pin[source];
    }
    uint8_t  getCableTempPin(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ? _cable_temp_pin[source] : OPENEVSE_CABLE_TEMP_PIN_NONE;
    }
    uint32_t getCableTempR25(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ? _cable_temp_r25[source] : 0;
    }
    uint32_t getCableTempBeta(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ? _cable_temp_beta[source] : 0;
    }
    int32_t getCableTempOffsetC10(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ? _cable_temp_offset_c10[source] : 0;
    }
    int32_t getCableTempPanicC10(uint8_t source) {
      return source < OPENEVSE_CABLE_TEMP_SOURCE_COUNT ? _cable_temp_panic_c10[source] : 0;
    }
    bool isCableTempEnabled() {
      // The controller has no dedicated read-back flag for $FF C. Prefer
      // what this session actually commanded and had accepted; fall back to
      // inferring from the sources (reports NOT_INSTALLED on all of them
      // while off) only when nothing has been commanded yet, e.g. fresh
      // after boot.
      return _cable_temp_known && (_cable_temp_commanded || !isCableTempAllNotInstalled());
    }
    bool isCableTempAllNotInstalled() {
      for(uint8_t i = 0; i < OPENEVSE_CABLE_TEMP_SOURCE_COUNT; i++) {
        if(OPENEVSE_CABLE_TEMP_STATUS_NOT_INSTALLED != _cable_temp_status[i]) return false;
      }
      return true;
    }
    void enableCableTemp(bool enabled, std::function<void(int ret)> callback = NULL);
    // Write one source's full configuration, then re-read it back so the
    // cache reflects what the controller actually accepted.
    void setCableTempConfig(uint8_t source, uint8_t pin, uint32_t r25, uint32_t beta,
                            int32_t offset_c10, int32_t panic_c10,
                            std::function<void(int ret)> callback = NULL);
    // Reassign a source's input pin, leaving its calibration alone.
    void setCableTempPin(uint8_t source, uint8_t pin, std::function<void(int ret)> callback = NULL);
#endif // ENABLE_CABLE_TEMP
    // Manually run the controller's stuck-relay recovery cycle (requires
    // firmware 9.3.0+ / ADVPWR). NAK'd by the controller if an EV is
    // connected. Blocking on the controller side for up to ~30s.
    void runStuckRelayRecovery(std::function<void(int ret)> callback = NULL);
    // Reset the relay-health accumulator, self-learned baselines, and the
    // stuck-relay recovery counter (requires the controller's RELAY_HEALTH
    // feature) - use after a physical relay replacement, since the estimate
    // is otherwise meaningless: it carries over wear from the old relay.
    void resetRelayHealth(std::function<void(int ret)> callback = NULL);
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
