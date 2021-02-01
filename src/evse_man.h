#ifndef _OPENEVSE_EVSE_MAN_H
#define _OPENEVSE_EVSE_MAN_H

#include <Arduino.h>
#include <RapiSender.h>
#include <openevse.h>
#include <MicroTasks.h>

#include "evse_monitor.h"

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

#define EvseClient_OpenEnergyMonitor_DemandShaper EVC(EvseClient_Vendor_OpenEnergyMonitor, 0x0001)

#define EvseClient_NULL                       ((EvseClient)UINT32_MAX)

#define EvseManager_Priority_Default    10
#define EvseManager_Priority_Divert     50
#define EvseManager_Priority_Timer     100
#define EvseManager_Priority_Boost     200
#define EvseManager_Priority_Manual   1000
#define EvseManager_Priority_Limit    1100
#define EvseManager_Priority_Ohm      1500
#define EvseManager_Priority_Error   10000

#ifndef EVSE_MANAGER_MAX_CLIENT_CLAIMS
#define EVSE_MANAGER_MAX_CLIENT_CLAIMS 10
#endif // !EVSE_MANAGER_MAX_CLIENT_CLAIMS

class EvseState
{
  public:
    enum Value : uint8_t {
      None,
      Active,
      Disabled
    };

  EvseState() = default;
  constexpr EvseState(Value value) : _value(value) { }

  bool fromString(const char *value)
  {
    // Cheat a bit and just check the first char
    if('a' == value[0] || 'd' == value[0]) {
      _value = 'a' == value[0] ? EvseState::Active : EvseState::Disabled;
      return true;
    }
    return false;
  }

  const char *toString()
  {
    return EvseState::Active == _value ? "active" :
           EvseState::Disabled == _value ? "disabled" :
           EvseState::None == _value ? "none" :
           "unknown";
  }

  operator Value() const { return _value; }
  explicit operator bool() = delete;        // Prevent usage: if(state)
  EvseState operator= (const Value val) {
    _value = val;
    return *this;
  }

  private:
    Value _value;
};

class EvseProperties
{
  private:
    EvseState _state;
    uint32_t _charge_current;
    uint32_t _max_current;
    uint32_t _energy_limit;
    uint32_t _time_limit;
    bool _auto_release;
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

    // Get/set the energy max to transfer for this charge session/client, after which the default
    // session state will be set to EvseState::Disabled and the client automatically released.
    uint32_t getEnergyLimit() {
      return _energy_limit;
    }
    void setEnergyLimit(uint32_t energy_limit) {
      _energy_limit = energy_limit;
    }

    // Get/set the time to stop the charge session/client, after which the default session state
    // will be set to EvseState::Disabled and the client automatically released.
    uint32_t getTimeLimit() {
      return _time_limit;
    }
    void setTimeLimit(uint32_t time_limit) {
      _time_limit = time_limit;
    }

    // Get/set the client auto release state. With the client auto release enabled the client claim
    // will automatically be released at the end of the charging session.
    bool getAutoRelease() {
      return _auto_release;
    }
    void setAutoRelease(bool auto_release) {
      _auto_release = auto_release;
    }

    EvseProperties & operator = (EvseProperties &rhs);
    EvseProperties & operator = (EvseState &rhs) {
      _state = rhs;
      return *this;
    }
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

        void claim(EvseClient client, int priority, EvseProperties &target);
        void release();

        bool isValid() {
          return _client != EvseClient_NULL;
        }

        bool operator==(EvseClient rhs) const {
          return _client == rhs;
        };

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

        uint32_t getEnergyLimit() {
          return _properties.getEnergyLimit();
        }

        uint32_t getTimeLimit() {
          return _properties.getTimeLimit();
        }

        bool getAutoRelease() {
          return _properties.getAutoRelease();
        }

        EvseProperties &getProperties() {
          return _properties;
        }
    };

    RapiSender _sender;
    EvseMonitor _monitor;

    Claim _clients[EVSE_MANAGER_MAX_CLIENT_CLAIMS];

    MicroTasks::EventListener _evseStateListener;

    EvseProperties _targetProperties;
    bool _hasClaims;

    bool _sleepForDisable;

    bool _evaluateClaims;
    bool _evaluateTargetState;
    int _waitingForEvent;

    void initialiseEvse();
    bool findClaim(EvseClient client, Claim **claim = NULL);
    bool evaluateClaims(EvseProperties &properties);

    bool setTargetState(EvseProperties &properties);

    EvseState getActiveState() {
      return _monitor.isDisabled() ? EvseState::Disabled : EvseState::Active;
    }

    EvseProperties &getClaimProperties(EvseClient client);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    EvseManager(Stream &port);
    ~EvseManager();

    bool begin();

    bool claim(EvseClient client, int priority, EvseProperties &target);
    bool release(EvseClient client);
    bool clientHasClaim(EvseClient client);

    EvseState getState(EvseClient client = EvseClient_NULL);
    uint32_t getChargeCurrent(EvseClient client = EvseClient_NULL);
    uint32_t getMaxCurrent(EvseClient client = EvseClient_NULL);
    uint32_t getEnergyLimit(EvseClient client = EvseClient_NULL);
    uint32_t getTimeLimit(EvseClient client = EvseClient_NULL);

    // Evse Status
    bool isConnected() {
      return OpenEVSE.isConnected();
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
    bool isVehicleConnected() {
      return _monitor.isVehicleConnected();
    }
    double getAmps() {
      return _monitor.getAmps();
    }
    double getVoltage() {
      return _monitor.getVoltage();
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
    long getFaultCountGFCI() {
      return _monitor.getFaultCountGFCI();
    }
    long getFaultCountNoGround() {
      return _monitor.getFaultCountNoGround();
    }
    long getFaultCountStuckRelay() {
      return _monitor.getFaultCountStuckRelay();
    }
    double getTempurature(uint8_t sensor) {
      return _monitor.getTempurature(sensor);
    }
    double isTempuratureValid(uint8_t sensor) {
      return _monitor.isTempuratureValid(sensor);
    }
    bool getDiodeCheckDisabled() {
      return _monitor.getDiodeCheckDisabled();
    }
    bool getVentRequiredDisabled() {
      return _monitor.getVentRequiredDisabled();
    }
    bool getGroundCheckDisabled() {
      return _monitor.getGroundCheckDisabled();
    }
    bool getStuckRelayCheckDisabled() {
      return _monitor.getStuckRelayCheckDisabled();
    }
    bool getGfiTestDisabled() {
      return _monitor.getGfiTestDisabled();
    }
    bool getTemperatureCheckDisabled() {
      return _monitor.getTemperatureCheckDisabled();
    }
    bool getButtonDisabled() {
      return _monitor.getButtonDisabled();
    }
    bool getAutoStartDisabled() {
      return _monitor.getAutoStartDisabled();
    }
    bool getSerialDebugEnabled() {
      return _monitor.getSerialDebugEnabled();
    }
    EvseMonitor::ServiceLevel getServiceLevel() {
      return _monitor.getServiceLevel();
    }
    EvseMonitor::LcdType getLcdType() {
      return _monitor.getLcdType();
    }
    const char *getFirmwareVersion() {
      return _monitor.getFirmwareVersion();
    }

    // Temp until everything uses EvseManager
    RapiSender &getSender() {
      return _sender;
    }

    // Get the OpenEVSE API
    OpenEVSEClass &getOpenEVSE() {
      return OpenEVSE;
    }

    // Register for events
    void onStateChange(MicroTasks::EventListener *listner) {
      _monitor.onStateChange(listner);
    }
    void onDataReady(MicroTasks::EventListener *listner) {
      _monitor.onDataReady(listner);
    }
    void onBootReady(MicroTasks::EventListener *listner) {
      _monitor.onBootReady(listner);
    }
};

#endif // !_OPENEVSE_EVSE_MAN_H
