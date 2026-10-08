#ifndef _SHELLY_LNM_H
#define _SHELLY_LNM_H

// Shelly Local Network Messaging (LNM) local power / voltage source
//
// Listens to Shelly LNM UDP multicast status messages and keeps the most
// recent grid active power and voltage values. When enabled, it replaces the
// MQTT feeds for:
//  - the divert grid excess feed (mqtt_grid_ie)
//  - the current shaper live power feed (mqtt_live_pwr), unless a dedicated
//    live power topic distinct from mqtt_grid_ie is configured
//  - the grid voltage feed (mqtt_vrms)
//
// Wire protocol and JSON payload are documented at:
// - https://shelly-api-docs.shelly.cloud/gen2/General/LocalNetworkMessaging/
// - https://shelly-api-docs.shelly.cloud/gen2/DynamicComponents/LNM/

#ifndef SHELLY_LNM_LOOP_TIME
#define SHELLY_LNM_LOOP_TIME 250 // ms, time between packet processing polls
#endif

#ifndef SHELLY_LNM_RETRY_TIME
#define SHELLY_LNM_RETRY_TIME 5000 // ms, time between listener start attempts
#endif

#ifndef SHELLY_LNM_APPLY_INTERVAL
#define SHELLY_LNM_APPLY_INTERVAL 5000 // ms, minimum time between applying values
#endif

#ifndef SHELLY_LNM_VOLTAGE_STEP
#define SHELLY_LNM_VOLTAGE_STEP 1.0 // V, smaller voltage changes are ignored
#endif

#define SHELLY_LNM_BUFFER_SIZE 1460 // max UDP payload on a 1500 MTU

#include <Arduino.h>
#include <WiFiUdp.h>
#include <MicroTasks.h>

#include "app_config.h"

class ShellyLnmTask : public MicroTasks::Task
{
  private:
    WiFiUDP _udp;
    bool _listening;
    char _listenAddr[16];
    uint16_t _listenPort;
    // last accepted measurements
    double _power;
    double _voltage;
    uint32_t _lastUpdate;
    // packet buffer, allocated only while listening
    uint8_t *_buffer;
    // values received but not yet applied to divert / shaper / EVSE
    bool _pendingPower;
    bool _pendingVoltage;
    uint32_t _lastApply;
    double _appliedVoltage;

    bool isConfigChanged();
    void processPacket(const uint8_t *data, size_t len);
    void applyPending();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    ShellyLnmTask();

    void begin();
    void start();
    void stop();
    bool isListening();
    void notifyConfigChanged();

    double getPower() {
      return _power;
    }

    double getVoltage() {
      return _voltage;
    }

    uint32_t getLastUpdate() {
      return _lastUpdate;
    }

    uint32_t getDataAge() {
      return 0 == _lastUpdate ? UINT32_MAX : millis() - _lastUpdate;
    }
};

extern ShellyLnmTask shelly_lnm;

#endif // _SHELLY_LNM_H