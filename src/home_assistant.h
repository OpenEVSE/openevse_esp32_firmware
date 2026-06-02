#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <MongooseHttpClient.h>

#include "ha_oauth.h"

class HomeAssistantClient : public MicroTasks::Task {
  public:
    HomeAssistantClient();

    void begin();

    bool isConnected();
    void getStatus(JsonDocument &doc);

    String beginAuthorize(const String &host, bool secure);
    bool handleCallback(const String &code, const String &state, String &error);
    void disconnect();
    void notifyConfigChanged();

    // Returns true if the request was dispatched. Returns false (and sends nothing)
    // when not connected or when a token refresh is due -- callers must not assume
    // onResponse will fire in that case.
    bool get(const String &path, MongooseHttpResponseHandler onResponse);

  protected:
    void setup() override;
    unsigned long loop(MicroTasks::WakeReason reason) override;

  private:
    MongooseHttpClient _client;

    String _pendingState;
    String _pendingClientId;
    unsigned long _pendingStateTime; // millis() when issued; 0 = none

    bool _refreshInFlight;
    unsigned long _lastRefreshAttempt;
    unsigned long _lastVehiclePoll;
    bool _vehicleInFlight;
    unsigned long _vehiclePollStart;

    void exchangeCode(const String &code);
    void refreshTokens();
    void storeTokens(const HaTokens &t);
    void pollVehicle();
    void pollVehicleField(int field); // 0=soc, 1=range, 2=eta; chains to the next
};

extern HomeAssistantClient homeAssistant;

#endif // HOME_ASSISTANT_H
