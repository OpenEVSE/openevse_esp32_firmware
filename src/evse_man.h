#ifndef _OPENEVSE_EVSE_MAN_H
#define _OPENEVSE_EVSE_MAN_H

#include <Arduino.h>
#include <RapiSender.h>
#include <openevse.h>
#include <MicroTasks.h>

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

typedef enum {
  EvseState_Active,
  EvseState_Disabled,
  EvseState_NULL
} EvseState;

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
    // session state will be set to EvseState_Disabled and the client automatically released.
    uint32_t getEnergyLimit() {
      return _energy_limit;
    }
    void setEnergyLimit(uint32_t energy_limit) {
      _energy_limit = energy_limit;
    }

    // Get/set the time to stop the charge session/client, after which the default session state
    // will be set to EvseState_Disabled and the client automatically released.
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
          return _client == EvseClient_NULL;
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
    };

    class EvseStateEvent : public MicroTasks::Event
    {
      private:
        uint8_t _evse_state;
        uint8_t _pilot_state;
      public:
        EvseStateEvent();
        void setState(uint8_t evse_state, uint8_t pilot_state)
        {
          _evse_state = evse_state;
          _pilot_state = pilot_state;
          Trigger();
        }
        uint8_t getState() {
          return _evse_state;
        }

        EvseState getEvseState() {
          return isDisabled() ? EvseState_Disabled : EvseState_Active;
        }
        bool isActive() {
          return OPENEVSE_STATE_NOT_CONNECTED <= _evse_state && _evse_state <= OPENEVSE_STATE_CHARGING;
        }
        bool isDisabled() {
          return OPENEVSE_STATE_SLEEPING <= _evse_state;
        }
        bool isError() {
          return OPENEVSE_STATE_VENT_REQUIRED <= _evse_state && _evse_state <= OPENEVSE_STATE_OVER_CURRENT;
        }
    };

    RapiSender _sender;

    Claim _clients[EVSE_MANAGER_MAX_CLIENT_CLAIMS];

    EvseStateEvent _state;
    MicroTasks::EventListener _evseStateListener;

    bool _sleepForDisable;

    void initialiseEvse();
    bool findClaim(EvseClient client, Claim **claim = NULL);
    bool evaluateClaims(EvseProperties &properties);

    void setEvseState(EvseState state);
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
      return _state.getState();
    }

    // Temp until everything uses EvseManager
    RapiSender &getSender() {
      return _sender;
    }

    // Register for events
    void onStateChange(MicroTasks::EventListener *listner) {
      _state.Register(listner);
    }
};

#endif // !_OPENEVSE_EVSE_MAN_H
