#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HOME_ASSISTANT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <time.h>
#include <esp_random.h>

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

// --- flow methods implemented in Tasks 8 & 9 ---
String HomeAssistantClient::beginAuthorize(const String &host, bool secure) {
  if (ha_url.length() == 0 || host.length() == 0) {
    return "";
  }

  std::string scheme = secure ? "https" : "http";
  std::string base = ha_derive_base_url(scheme, std::string(host.c_str()));
  std::string clientId = base + "/";
  std::string redirectUri = ha_build_redirect_uri(base);

  // 16 hex chars of CSRF state from the HW RNG.
  char stateBuf[17];
  for (int i = 0; i < 16; i++) {
    stateBuf[i] = "0123456789abcdef"[esp_random() & 0xF];
  }
  stateBuf[16] = '\0';

  _pendingState = stateBuf;
  _pendingClientId = clientId.c_str();
  _pendingStateTime = millis();
  if (_pendingStateTime == 0) _pendingStateTime = 1; // 0 means "none"; millis()==0 at boot/wrap is a benign miss

  // Persist the client_id so token refresh can replay it after a reboot
  // (it is derived from the device Host, which we don't otherwise know later).
  ha_client_id = clientId.c_str();
  config_commit();

  std::string url = ha_build_authorize_url(
      std::string(ha_url.c_str()), clientId, redirectUri, _pendingState.c_str());
  return String(url.c_str());
}

bool HomeAssistantClient::handleCallback(const String &code, const String &state, String &error) {
  if (_pendingStateTime == 0) {
    error = "no pending authorization";
    return false;
  }
  if (state.length() == 0 || state != _pendingState) {
    error = "state mismatch";
    return false;
  }
  if (code.length() == 0) {
    error = "missing code";
    return false;
  }
  // Consume the pending state (single use); keep _pendingClientId for the exchange.
  _pendingStateTime = 0;
  _pendingState = "";
  exchangeCode(code);
  return true;
}

void HomeAssistantClient::storeTokens(const HaTokens &t) {
  ha_access_token = t.access_token.c_str();
  if (!t.refresh_token.empty()) {
    ha_refresh_token = t.refresh_token.c_str();
  }
  // If NTP isn't synced yet (ha_now_unix()==0), expiry lands near epoch, causing an
  // immediate re-refresh once the clock syncs -- safe degradation.
  ha_token_expires = ha_compute_expiry(ha_now_unix(), t.expires_in);
  config_commit();

  StaticJsonDocument<128> ev;
  ev["home_assistant"] = "connected";
  event_send(ev);
}

void HomeAssistantClient::exchangeCode(const String &code) {
  if (ha_url.length() == 0 || _pendingClientId.length() == 0) {
    DBUGLN("[ha] exchangeCode: missing ha_url or client_id");
    return;
  }
  std::string body = ha_build_token_exchange_body(
      std::string(_pendingClientId.c_str()), std::string(code.c_str()));

  String uri = ha_url;
  while (uri.endsWith("/")) uri.remove(uri.length() - 1);
  uri += "/auth/token";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_POST);
  req->addHeader("Content-Type", "application/x-www-form-urlencoded");
  // Safe: send() -> mg_connect_http_opt() copies the body into the connection's
  // send buffer synchronously, while `body` is still in scope here.
  req->setContent((const uint8_t *)body.c_str(), body.length());
  req->onResponse([this](MongooseHttpClientResponse *response) {
    if (response->respCode() == 200) {
      HaTokens t;
      MongooseString respBody = response->body();
      std::string json((const char *)respBody, respBody.length());
      if (ha_parse_token_response(json, t)) {
        storeTokens(t);
        return;
      }
    }
    StaticJsonDocument<128> ev;
    ev["home_assistant"] = "error";
    ev["home_assistant_error"] = "token exchange failed";
    event_send(ev);
  });
  _client.send(req);
}

void HomeAssistantClient::refreshTokens() {}

void HomeAssistantClient::get(const String &path, MongooseHttpResponseHandler onResponse) {
  (void)path; (void)onResponse;
}
