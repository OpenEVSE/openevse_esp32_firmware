#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HOME_ASSISTANT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <time.h>

#include "home_assistant.h"
#include "app_config.h"
#include "debug.h"
#include "event.h"

#define HA_PENDING_STATE_TTL_MS   (5 * 60 * 1000UL)
#define HA_REFRESH_MARGIN_SEC     300
#define HA_REFRESH_RETRY_MS       (60 * 1000UL)
#define HA_LOOP_INTERVAL_MS       (30 * 1000UL)

HomeAssistantClient homeAssistant;

static uint64_t ha_now_unix() {
  time_t now = time(nullptr);
  return (now > 0) ? (uint64_t)now : 0;
}

HomeAssistantClient::HomeAssistantClient() :
  MicroTasks::Task(),
  _pendingStateTime(0),
  _refreshInFlight(false),
  _lastRefreshAttempt(0)
{
}

void HomeAssistantClient::begin() {
  MicroTask.startTask(this);
}

void HomeAssistantClient::setup() {
}

bool HomeAssistantClient::isConnected() {
  if (!config_home_assistant_enabled()) return false;
  // Connected once OAuth is complete: a refresh token means the session exists;
  // the background refresh loop keeps the access token current.
  return ha_refresh_token.length() > 0;
}

void HomeAssistantClient::getStatus(JsonDocument &doc) {
  doc["enabled"] = config_home_assistant_enabled();
  doc["connected"] = isConnected();
  doc["ha_url"] = ha_url;
  doc["expires"] = ha_token_expires;
}

void HomeAssistantClient::notifyConfigChanged() {
  MicroTask.wakeTask(this);
}

void HomeAssistantClient::disconnect() {
  ha_access_token = "";
  ha_refresh_token = "";
  ha_token_expires = 0;
  _pendingStateTime = 0;
  _pendingState = "";
  _pendingClientId = "";
  config_commit();
  StaticJsonDocument<128> ev;
  ev["home_assistant"] = "disconnected";
  event_send(ev);
}

unsigned long HomeAssistantClient::loop(MicroTasks::WakeReason reason) {
  if (_pendingStateTime != 0 &&
      (millis() - _pendingStateTime) > HA_PENDING_STATE_TTL_MS) {
    _pendingStateTime = 0;
    _pendingState = "";
    _pendingClientId = "";
  }
  // Refresh scheduling implemented in Task 9.
  return HA_LOOP_INTERVAL_MS;
}

// --- flow methods implemented in Tasks 8 & 9 (stubs for now) ---
String HomeAssistantClient::beginAuthorize(const String &host, bool secure) {
  (void)host; (void)secure;
  return "";
}

bool HomeAssistantClient::handleCallback(const String &code, const String &state, String &error) {
  (void)code; (void)state;
  error = "not implemented";
  return false;
}

void HomeAssistantClient::exchangeCode(const String &code) { (void)code; }
void HomeAssistantClient::refreshTokens() {}
void HomeAssistantClient::storeTokens(const HaTokens &t) { (void)t; }

void HomeAssistantClient::get(const String &path, MongooseHttpResponseHandler onResponse) {
  (void)path; (void)onResponse;
}
