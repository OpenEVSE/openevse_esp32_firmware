#ifndef _OPENEVSE_EVSE_MAN_H
#define _OPENEVSE_EVSE_MAN_H

#include <Arduino.h>
#include <RapiSender.h>
#include <openevse.h>
#include <MicroTasks.h>

#include "evse_state.h"
#include "evse_monitor.h"
#include "event_log.h"
#include "json_serialize.h"
#include "app_config.h"

typedef uint32_t EvseClient;

#define EvseClient_Vendor_SHIFT               16

#define EvseClient_Vendor_OpenEVSE            0x0001
#define EvseClient_Vendor_OpenEnergyMonitor   0x0002
#define EvseClient_Vendor_BigJungle           0x0003

#define EvseClient_Vendor_Unregistered        0xFFFE
#define EvseClient_Vendor_Reserved            0xFFFF

#define EVC(Vendor, Client)                   ((EvseClient)((((Vendor) << EvseClient_Vendor_SHIFT) | (Client))))

#define EvseClient_OpenEVSE_Manual            EVC(EvseClient_Vendor_OpenEVSE, 0x0001)
#define EvseClient_OpenEVSE_Divert            EVC(EvseClient_Vendor_OpenEVSE, 0x0002)
#define EvseClient_OpenEVSE_Boost             EVC(EvseClient_Vendor_OpenEVSE, 0x0003)
#define EvseClient_OpenEVSE_Schedule          EVC(EvseClient_Vendor_OpenEVSE, 0x0004)
#define EvseClient_OpenEVSE_Limit             EVC(EvseClient_Vendor_OpenEVSE, 0x0006)
#define EvseClient_OpenEVSE_Error             EVC(EvseClient_Vendor_OpenEVSE, 0x0007)
#define EvseClient_OpenEVSE_Ohm               EVC(EvseClient_Vendor_OpenEVSE, 0x0008)
#define EvseClient_OpenEVSE_OCPP              EVC(EvseClient_Vendor_OpenEVSE, 0x0009)
#define EvseClient_OpenEVSE_RFID              EVC(EvseClient_Vendor_OpenEVSE, 0x000A)
#define EvseClient_OpenEVSE_MQTT              EVC(EvseClient_Vendor_OpenEVSE, 0x000B)
#define EvseClient_OpenEVSE_Shaper            EVC(EvseClient_Vendor_OpenEVSE, 0x000C)
#define EvseClient_OpenEVSE_TempThrottle      EVC(EvseClient_Vendor_OpenEVSE, 0x000D)
#define EvseClient_OpenEVSE_LoadSharing       EVC(EvseClient_Vendor_OpenEVSE, 0x000E)

#define EvseClient_OpenEnergyMonitor_DemandShaper EVC(EvseClient_Vendor_OpenEnergyMonitor, 0x0001)

#define EvseClient_NULL                       ((EvseClient)UINT32_MAX)

#define EvseManager_Priority_Default    10
#define EvseManager_Priority_Divert     50
#define EvseManager_Priority_Timer     100
#define EvseManager_Priority_Boost     200
#define EvseManager_Priority_API       500
#define EvseManager_Priority_MQTT      500
#define EvseManager_Priority_Ohm       500
#define EvseManager_Priority_Manual   1000
#define EvseManager_Priority_RFID     1030
#define EvseManager_Priority_OCPP     1050
#define EvseManager_Priority_Limit    1100
#define EvseManager_Priority_Safety   5000
#define EvseManager_Priority_Error   10000

#define EVSE_VEHICLE_SOC    (1 << 0)
#define EVSE_VEHICLE_RANGE  (1 << 1)
#define EVSE_VEHICLE_ETA    (1 << 2)
#define EVSE_VEHICLE_CHARGE_LIMIT (1 << 3)

#ifndef EVSE_MANAGER_MAX_CLIENT_CLAIMS
#define EVSE_MANAGER_MAX_CLIENT_CLAIMS 10
#endif // !EVSE_MANAGER_MAX_CLIENT_CLAIMS

class EvseProperties : virtual public JsonSerialize<512>
{
  private:
    EvseState _state;
    uint32_t _charge_current;
    uint32_t _max_current;
    bool _auto_release;
    bool _has_auto_release = false;
  public:
    EvseProperties();
    EvseProperties(EvseState state);

    void clear();

    // Get/set the EVSE state, either active or disabled
    EvseState getState() {
      return _state;
    }
    void setState(EvseState state) {
      _state = state;
    }

    // Get/set charge current,
    uint32_t getChargeCurrent() {
      return _charge_current;
    }
    void setChargeCurrent(uint32_t charge_current) {
      _charge_current = charge_current;
    }

    // Get/set the max current, overides limits the charge current (irrespective of priority) but
    // does not override the configured max charge current of the hardware.
    uint32_t getMaxCurrent() {
      return _max_current;
    }
    void setMaxCurrent(uint32_t max_current) {
      _max_current = max_current;
    }

    // Get/set the client auto release state. With the client auto release enabled the client claim
    // will automatically be released at the end of the charging session.
    bool isAutoRelease() {
      return _auto_release;
    }

    bool hasAutoRelease() {
      return _has_auto_release;
    }

    void setAutoRelease(bool auto_release) {
      _auto_release = auto_release;
      _has_auto_release = true;
    }

    EvseProperties & operator = (EvseProperties &rhs);
    EvseProperties & operator = (EvseState &rhs) {
      _state = rhs;
      return *this;
    }

    bool equals(EvseProperties &rhs) {
      return this->_state == rhs._state &&
             this->_charge_current == rhs._charge_current &&
             this->_max_current == rhs._max_current &&
             this->_auto_release == rhs._auto_release;

    }
    bool equals(EvseState &rhs) {
      return this->_state == rhs;
    }

    bool operator == (EvseProperties &rhs) {
      return this->equals(rhs);
    }
    bool operator == (EvseState &rhs) {
      return this->equals(rhs);
    }

    bool operator != (EvseProperties &rhs) {
      return !equals(rhs);
    }
    bool operator != (EvseState &rhs) {
      return !equals(rhs);
    }


    using JsonSerialize::deserialize;
    virtual bool deserialize(JsonObject &obj);
    using JsonSerialize::serialize;
    virtual bool serialize(JsonObject &obj);
};

class EvseManager : public MicroTasks::Task
{
  private:
    class Claim
    {
      private:
        EvseClient _client;
        int _priority;
        EvseProperties _properties;

      public:
        Claim();

        bool claim(EvseClient client, int priority, EvseProperties &target);
        void release();

        bool isValid() {
          return _client != EvseClient_NULL;
        }

        bool operator==(EvseClient rhs) const {
          return _client == rhs;
        }

        EvseClient getClient() {
          return _client;
        }

        int getPriority() {
          return _priority;
        }

        EvseState getState() {
          return _properties.getState();
        }

        uint32_t getChargeCurrent() {
          return _properties.getChargeCurrent();
        }

        uint32_t getMaxCurrent() {
          return _properties.getMaxCurrent();
        }

        bool isAutoRelease() {
          return _properties.isAutoRelease();
        }

        bool hasAutoRelease() {
          return _properties.hasAutoRelease();
        }

        EvseProperties &getProperties() {
          return _properties;
        }
    };

    RapiSender _sender;
    OpenEVSEClass _openevse;
    EvseMonitor _monitor;
    EventLog &_eventLog;

    Claim _clients[EVSE_MANAGER_MAX_CLIENT_CLAIMS];

    MicroTasks::EventListener _evseStateListener;
    MicroTasks::EventListener _evseBootListener;
    MicroTasks::EventListener _sessionCompleteListener;
    MicroTasks::EventListener _settingsChangedListener;

    EvseProperties _targetProperties;
    bool _hasClaims;
    uint8_t _version;

    EvseClient _state_client;
    EvseClient _charge_current_client;
    EvseClient _max_current_client;

    bool _sleepForDisable;

    bool _evaluateClaims;
    bool _evaluateTargetState;

    uint32_t _vehicleValid;
    uint32_t _vehicleUpdated;
    uint32_t _vehicleLastUpdated;
    int _vehicleStateOfCharge;
    int _vehicleRange;
    int _vehicleEta;
    int _vehicleChargeLimit;

    void initialiseEvse();
    bool findClaim(EvseClient client, Claim **claim = NULL);
    bool evaluateClaims(EvseProperties &properties);
    void releaseAutoReleaseClaims();

    bool setTargetState(EvseProperties &properties);

    EvseState getActiveState() {
      return _monitor.isDisabled() ? EvseState::Disabled : EvseState::Active;
    }

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    EvseManager(Stream &port, EventLog &eventLog);
    ~EvseManager();

    bool begin();

    bool claim(EvseClient client, int priority, EvseProperties &target);
    bool release(EvseClient client);
    bool clientHasClaim(EvseClient client);
    uint8_t getClaimsVersion();

    EvseProperties &getClaimProperties(EvseClient client);
    EvseState getState(EvseClient client = EvseClient_NULL);
    uint32_t getChargeCurrent(EvseClient client = EvseClient_NULL);
    uint32_t getMaxCurrent(EvseClient client = EvseClient_NULL);

    // Get the client whose claim is currently setting the state/charge current,
    // EvseClient_NULL if no active claim sets the property
    EvseClient getStateClient() {
      return _state_client;
    }
    EvseClient getChargeCurrentClient() {
      return _charge_current_client;
    }

    bool serializeClaims(DynamicJsonDocument &doc);
    bool serializeClaim(DynamicJsonDocument &doc, EvseClient client);
    bool serializeTarget(DynamicJsonDocument &doc);

    // Evse Status
    bool isConnected() {
      return _openevse.isConnected();
    }
    bool isActive() {
      return getActiveState() == EvseState::Active;
    }
    uint8_t getEvseState() {
      return _monitor.getEvseState();
    }
    uint8_t getPilotState() {
      return _monitor.getPilotState();
    }
    uint32_t getFlags() {
      return _monitor.getFlags();
    }
    uint8_t getStateColour();
    bool isVehicleConnected() {
      return _monitor.isVehicleConnected();
    }
    bool isBootLocked() {
      return _monitor.isBootLocked();
    }
    bool isError() {
      return _monitor.isError();
    }
    bool isCharging() {
      return _monitor.isCharging();
    }
    double getAmps() {
      return _monitor.getAmps();
    }
    double getVoltage() {
      return _monitor.getVoltage();
    }
    double getPower() {
      return _monitor.getPower();
    }
    void setVoltage(double volts) {
      _monitor.setVoltage(volts);
    }
    void setMqttVoltage(double volts) {
      _monitor.setMqttVoltage(volts);
    }
    uint32_t getSessionElapsed() {
      return _monitor.getSessionElapsed();
    }
    double getSessionEnergy() {
      return _monitor.getSessionEnergy();
    }
    double getTotalEnergy() {
      return _monitor.getTotalEnergy();
    }
    double getTotalDay() {
      return _monitor.getTotalDay();
    }
    double getTotalWeek() {
      return _monitor.getTotalWeek();
    }
    double getTotalMonth() {
      return _monitor.getTotalMonth();
    }
    double getTotalYear() {
      return _monitor.getTotalYear();
    }
    bool saveEnergyMeter() {
      return _monitor.saveEnergyMeter();
    }
    bool resetEnergyMeter(bool full, bool import) {
      return _monitor.resetEnergyMeter(full, import);
    }
    bool publishEnergyMeter() {
      return _monitor.publishEnergyMeter();
    }
    void createEnergyMeterJsonDoc(JsonDocument &doc) {
      _monitor.createEnergyMeterJsonDoc(doc);
    }
    long getFaultCountGFCI() {
      return _monitor.getFaultCountGFCI();
    }
    long getFaultCountNoGround() {
      return _monitor.getFaultCountNoGround();
    }
    long getFaultCountStuckRelay() {
      return _monitor.getFaultCountStuckRelay();
    }
    double getTemperature(uint8_t sensor) {
      return _monitor.getTemperature(sensor);
    }
    double isTemperatureValid(uint8_t sensor) {
      return _monitor.isTemperatureValid(sensor);
    }
    bool isDiodeCheckEnabled() {
      return _monitor.isDiodeCheckEnabled();
    }
    bool isVentRequiredEnabled() {
      return _monitor.isVentRequiredEnabled();
    }
    bool isGroundCheckEnabled() {
      return _monitor.isGroundCheckEnabled();
    }
    bool isStuckRelayCheckEnabled() {
      return _monitor.isStuckRelayCheckEnabled();
    }
    bool isGfiTestEnabled() {
      return _monitor.isGfiTestEnabled();
    }
    bool isTemperatureCheckEnabled() {
      return _monitor.isTemperatureCheckEnabled();
    }
    bool isOvercurrentMonitorEnabled() {
      return _monitor.isOvercurrentMonitorEnabled();
    }
    uint32_t getPanicTemperature() {
      return _monitor.getPanicTemperature();
    }
    bool isFrontButtonEnabled() {
      return _monitor.isFrontButtonEnabled();
    }
    bool isButtonDisabled() {
      return _monitor.isButtonDisabled();
    }
    bool isAutoStartDisabled() {
      return _monitor.isAutoStartDisabled();
    }
    bool isSerialDebugEnabled() {
      return _monitor.isSerialDebugEnabled();
    }
    EvseMonitor::ServiceLevel getActualServiceLevel() {
      return _monitor.getActualServiceLevel();
    }
    EvseMonitor::ServiceLevel getServiceLevel() {
      return _monitor.getServiceLevel();
    }
    void setServiceLevel(EvseMonitor::ServiceLevel level) {
      _monitor.setServiceLevel(level);
    }
    EvseMonitor::LcdType getLcdType() {
      return _monitor.getLcdType();
    }
    const char *getFirmwareVersion() {
      return _monitor.getFirmwareVersion();
    }
    const char *getSerial() {
      return _monitor.getSerial();
    }
    long getMinCurrent() {
      return _monitor.getMinCurrent();
    }
    void setMaxConfiguredCurrent(long amps);
    long getMaxConfiguredCurrent() {
      return _monitor.getMaxConfiguredCurrent();
    }
    void setMaxHardwareCurrent(long amps);
    long getMaxHardwareCurrent() {
      return _monitor.getMaxHardwareCurrent();
    }
    void configureCurrentSensorScale(long scale, long offset) {
      _monitor.configureCurrentSensorScale(scale, offset);
    }
    long getCurrentSensorScale() {
      return _monitor.getCurrentSensorScale();
    }
    long getCurrentSensorOffset() {
      return _monitor.getCurrentSensorOffset();
    }
    void getAmmeterSettings() {
      _monitor.getAmmeterSettings();
    }

    void enableFeature(uint8_t feature, bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableFeature(feature, enabled, callback);
    }
    void enableDiodeCheck(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableDiodeCheck(enabled, callback);
    }
    void enableGfiTestCheck(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableGfiTestCheck(enabled, callback);
    }
    void enableGroundCheck(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableGroundCheck(enabled, callback);
    }
    void enableStuckRelayCheck(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableStuckRelayCheck(enabled, callback);
    }
    void enableVentRequired(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableVentRequired(enabled, callback);
    }
    void enableTemperatureCheck(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableTemperatureCheck(enabled, callback);
    }
    void enableOvercurrentMonitor(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableOvercurrentMonitor(enabled, callback);
    }
    void setPanicTemperature(uint32_t tempC, std::function<void(int ret)> callback = NULL) {
      _monitor.setPanicTemperature(tempC, callback);
    }
    void enableFrontButton(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableFrontButton(enabled, callback);
    }
    bool isBootLockEnabled() {
      return _monitor.isBootLockEnabled();
    }
    void enableBootLock(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableBootLock(enabled, callback);
    }
    uint32_t getHeartbeatInterval() {
      return _monitor.getHeartbeatInterval();
    }
    uint32_t getHeartbeatCurrent() {
      return _monitor.getHeartbeatCurrent();
    }
    bool isHeartbeatEnabled() {
      return _monitor.isHeartbeatEnabled();
    }
    void setHeartbeatSupervision(uint32_t interval, uint32_t current, std::function<void(int ret)> callback = NULL) {
      _monitor.setHeartbeatSupervision(interval, current, callback);
    }
    bool isPPAutoAmpacityEnabled() {
      return _monitor.isPPAutoAmpacityEnabled();
    }
    void enablePPAutoAmpacity(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enablePPAutoAmpacity(enabled, callback);
    }
    bool isZeroCrossSwitchEnabled() {
      return _monitor.isZeroCrossSwitchEnabled();
    }
    void enableZeroCrossSwitch(bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.enableZeroCrossSwitch(enabled, callback);
    }
    bool isDC1RelayEnabled() { return _monitor.isDC1RelayEnabled(); }
    bool isDC2RelayEnabled() { return _monitor.isDC2RelayEnabled(); }
    bool isACRelayEnabled()  { return _monitor.isACRelayEnabled(); }
    bool isRelayStatusKnown() { return _monitor.isRelayStatusKnown(); }
    void setRelayEnable(int relay, bool enabled, std::function<void(int ret)> callback = NULL) {
      _monitor.setRelayEnable(relay, enabled, callback);
    }
    void resetFaultCounters(std::function<void(int ret)> callback = NULL) {
      _monitor.resetFaultCounters(callback);
    }
    uint32_t getFrequency() { return _monitor.getFrequency(); }
    const char *getChipId() { return _monitor.getChipId(); }
    bool isD9Supported() { return _monitor.isD9Supported(); }
    void restartEvse() {
      _monitor.restart();
    }

    // Get/set the vehicle state
    int getVehicleStateOfCharge() {
      return _vehicleStateOfCharge;
    }
    int getVehicleRange() {
      return _vehicleRange;
    }
    int getVehicleEta() {
      return _vehicleEta;
    }
    int getVehicleChargeLimit() {
      return _vehicleChargeLimit;
    }
    uint32_t getVehicleLastUpdated() {
      return _vehicleLastUpdated;
    }
    int isVehicleStateOfChargeValid() {
      return 0 != (_vehicleValid & EVSE_VEHICLE_SOC);
    }
    int isVehicleRangeValid() {
      return 0 != (_vehicleValid & EVSE_VEHICLE_RANGE);
    }
    int isVehicleEtaValid() {
      return 0 != (_vehicleValid & EVSE_VEHICLE_ETA);
    }
    int isVehicleChargeLimitValid() {
      return 0 != (_vehicleValid & EVSE_VEHICLE_CHARGE_LIMIT);
    }
    void setVehicleStateOfCharge(int vehicleStateOfCharge);
    void setVehicleRange(int vehicleRange);
    void setVehicleEta(int vehicleEta);
    void setVehicleChargeLimit(int vehicleChargeLimit);

    // Get/set the 'disabled' mode
    bool isSleepForDisable() {
      return _sleepForDisable;
    }
    void setSleepForDisable(bool sleepForDisable);

    // unlock openevse fw compiled with BOOTLOCK
    void unlock();

    // deserialize restart payloads
    bool restart(String payload);

    // Temp until everything uses EvseManager
    RapiSender &getSender() {
      return _sender;
    }

    // Get the OpenEVSE API. Must be the instance the monitor initialised with
    // the RAPI sender; the global OpenEVSE object is never begin()-ed so its
    // commands are silently dropped.
    OpenEVSEClass &getOpenEVSE() {
      return _openevse;
    }

    // Register for events
    void onStateChange(MicroTasks::EventListener *listner) {
      _monitor.onStateChange(listner);
    }
    void onSettingsChanged(MicroTasks::EventListener *listner) {
      _monitor.onSettingsChanged(listner);
    }
    void onDataReady(MicroTasks::EventListener *listner) {
      _monitor.onDataReady(listner);
    }
    void onBootReady(MicroTasks::EventListener *listner) {
      _monitor.onBootReady(listner);
    }
    void onSessionComplete(MicroTasks::EventListener *listner) {
      _monitor.onSessionComplete(listner);
    }

    bool isRapiCommandBlocked(String rapi);
};

#endif // !_OPENEVSE_EVSE_MAN_H
