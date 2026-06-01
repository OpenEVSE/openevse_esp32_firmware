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

    void get(const String &path, MongooseHttpResponseHandler onResponse);

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

    void exchangeCode(const String &code);
    void refreshTokens();
    void storeTokens(const HaTokens &t);
};

extern HomeAssistantClient homeAssistant;

#endif // HOME_ASSISTANT_H
