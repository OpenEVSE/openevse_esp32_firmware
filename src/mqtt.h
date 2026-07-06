#ifndef _EMONESP_MQTT_H
#define _EMONESP_MQTT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MongooseMqttClient.h>
#include <MicroTasks.h>

#include "emonesp.h"
#include "app_config.h"
#include "evse_man.h" // For EvseProperties, EvseClient_OpenEVSE_MQTT etc.
#include "limit.h"    // For LimitProperties
#include "event.h"
#include "scheduler.h" // For scheduler interaction
#include "manual.h"    // For manual override interaction
#include "divert.h"    // For divert interaction
#include "input.h"     // For solar, grid_ie
#include "espal.h"
#include "net_manager.h"
#include "certificates.h"
#include "current_shaper.h" // For shaper interaction, if any direct calls were made

// Forward declarations

#define MQTT_LOOP_INTERVAL 50 // ms, replaces MQTT_LOOP define
#define MQTT_CONNECT_TIMEOUT (5 * 1000) // ms

class Mqtt : public MicroTasks::Task {
  private:
    MongooseMqttClient _mqttclient;
    EvseManager *_evse; // Pointer to EvseManager instance

    // MQTT connection state and timing
    long _nextMqttReconnectAttempt = 0;
    unsigned long _mqttRestartTime = 0;
    bool _connecting = false;
    unsigned long _connectStartTime = 0;  // When the current connection attempt started
    unsigned long _error_time = 0; // To handle disconnect events properly

    // Version tracking for publishing updates
    uint8_t _claimsVersion = 0;
    uint8_t _overrideVersion = 0;
    uint8_t _scheduleVersion = 0;
    uint8_t _limitVersion = 0;
    uint32_t _configVersion = 0;

    String _lastWill = "";
    unsigned long _loop_timer = 0; // Timer for periodic publishing tasks

    // Status observability
    char   _brokerIp[46];       // resolved broker IP, "failed" on DNS error, or ""
    char   _brokerVersion[96];  // payload of $SYS/broker/version, or ""
    time_t _connectedSince;     // Unix ts when last connected (0 = never)
    time_t _lastRxTime;         // Unix ts of most recent broker traffic, sent or received (0 = never)
    bool   _needsDnsLookup = false; // set in onMqttConnect; DNS done safely in loop()
    unsigned long _lastStatusPush = 0; // millis() of last periodic status WebSocket push

    // Last failure cause, for troubleshooting in the UI
    char   _errorCategory[16];  // "", "auth", "unavailable", "id_rejected",
                                // "version", "network", "timeout", "dns"
    char   _errorDetail[64];    // human-readable detail (broker reason / strerror)
    void   setError(const char *category, const char *detail);

    // Properties for claims, overrides, limits
    EvseProperties _claim_props;
    EvseProperties _override_props;
    LimitProperties _limit_props;

    // Internal helper methods
    void attemptConnection();
    void onMqttConnect();
    void onMqttDisconnect(int err, const char *reason);
    void subscribeTopics();
    void publishInitialState();
    void checkAndPublishUpdates();

    // Callback for incoming MQTT messages
    // Made static because MongooseMqttClient might need a C-style function pointer
    // or we pass 'this' and use a wrapper. For simplicity, we can make it a member
    // and ensure the lambda captures 'this' correctly.
    // MongooseMqttClient::onMessage takes std::function, so member function is fine.
    void handleMqttMessage(MongooseString topic, MongooseString payload);

  protected:
    void setup() override;
    unsigned long loop(MicroTasks::WakeReason reason) override;

  public:
    Mqtt(EvseManager &evse);
    ~Mqtt();

    void begin(); // To start the task

    // Public interface (existing functions from mqtt.h, adapted)
    bool isConnected();
    void restartConnection();

    // Status accessors (used by GET /status and WebSocket events)
    const char *getMqttStatus();
    const char *getBrokerIp()      { return _brokerIp; }
    const char *getBrokerVersion() { return _brokerVersion; }
    time_t      getConnectedSince(){ return _connectedSince; }
    time_t      getLastRxTime()    { return _lastRxTime; }
    const char *getErrorCategory() { return _errorCategory; }
    const char *getErrorDetail()   { return _errorDetail; }

    // Publishing methods - these can be called from other modules
    void publishData(JsonDocument &data); // Generic data publish
    void publishConfig();
    void publishClaim();
    void setClaim(bool override, EvseProperties &props);
    void publishOverride();
    void publishSchedule();
    void setSchedule(String schedule);
    void clearSchedule(uint32_t event);
    void publishLimit();
    void setLimit(LimitProperties &limitProps);

    // Method to be called by other services when their state changes
    void notifyEvseClaimChanged();
    void notifyManualOverrideChanged();
    void notifyScheduleChanged();
    void notifyLimitChanged();
    void notifyConfigChanged();
};

extern Mqtt mqtt; // Global instance of the MQTT task

#endif // _EMONESP_MQTT_H