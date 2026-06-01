#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HOME_ASSISTANT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <stdlib.h>
#include <time.h>
#include <esp_random.h>

#include "home_assistant.h"
#include "app_config.h"
#include "debug.h"
#include "event.h"
#include <math.h>      // lround
#include "input.h"     // global EvseManager `evse`

#define HA_PENDING_STATE_TTL_MS   (5 * 60 * 1000UL)
#define HA_REFRESH_MARGIN_SEC     300
#define HA_REFRESH_RETRY_MS       (60 * 1000UL)
#define HA_REFRESH_TIMEOUT_MS     (30 * 1000UL)     // clear a stuck in-flight refresh
#define HA_LOOP_INTERVAL_MS       (30 * 1000UL)
#define HA_VEHICLE_POLL_MS        (30 * 1000UL)   // poll HA vehicle entities every 30 s
#define HA_VEHICLE_TIMEOUT_MS     (30 * 1000UL)   // clear a stuck vehicle-poll chain

HomeAssistantClient homeAssistant;

static uint64_t ha_now_unix() {
  time_t now = time(nullptr);
  return (now > 0) ? (uint64_t)now : 0;
}

HomeAssistantClient::HomeAssistantClient() :
  MicroTasks::Task(),
  _pendingStateTime(0),
  _refreshInFlight(false),
  _lastRefreshAttempt(0),
  _lastVehiclePoll(0),
  _vehicleInFlight(false),
  _vehiclePollStart(0)
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
  // Disconnecting turns the integration off too, so isConnected() reads false.
  config_set("home_assistant_enabled", false);
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
  // Recover if a refresh request never completed (e.g. connect failed and no
  // onResponse fired) -- otherwise _refreshInFlight would block refresh forever.
  if (_refreshInFlight && _lastRefreshAttempt != 0 &&
      (millis() - _lastRefreshAttempt) > HA_REFRESH_TIMEOUT_MS) {
    DBUGLN("[ha] refresh timed out, clearing in-flight flag");
    _refreshInFlight = false;
  }

  if (config_home_assistant_enabled() && ha_refresh_token.length() > 0 && !_refreshInFlight) {
    bool due = ha_refresh_due(ha_token_expires, ha_now_unix(), HA_REFRESH_MARGIN_SEC);
    bool backoffElapsed = (millis() - _lastRefreshAttempt) > HA_REFRESH_RETRY_MS;
    if (due && (_lastRefreshAttempt == 0 || backoffElapsed)) {
      refreshTokens();
    }
  }

  // Recover a stuck vehicle-poll chain (e.g. a request that never completed and
  // no onResponse fired) -- otherwise _vehicleInFlight would block polling forever.
  if (_vehicleInFlight && _vehiclePollStart != 0 &&
      (millis() - _vehiclePollStart) > HA_VEHICLE_TIMEOUT_MS) {
    DBUGLN("[ha] vehicle poll timed out, clearing in-flight flag");
    _vehicleInFlight = false;
  }

  // Poll HA vehicle entities when HA is the selected vehicle-data source.
  if (isConnected()
      && vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT
      && !_vehicleInFlight
      && (_lastVehiclePoll == 0 || (millis() - _lastVehiclePoll) >= HA_VEHICLE_POLL_MS)) {
    _lastVehiclePoll = millis();
    if (_lastVehiclePoll == 0) _lastVehiclePoll = 1; // 0 means "never polled"
    pollVehicle();
  }

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
  // A successful token grant IS the connection -- enable the service so isConnected()
  // and the refresh loop treat it as active (the user clicked Connect; there is no
  // separate enable toggle in v1).
  config_set("home_assistant_enabled", true);
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

void HomeAssistantClient::refreshTokens() {
  if (_refreshInFlight) return;
  if (ha_url.length() == 0 || ha_refresh_token.length() == 0) return;
  if (ha_client_id.length() == 0) return; // never authorized

  _refreshInFlight = true;
  _lastRefreshAttempt = millis();

  std::string clientId = std::string(ha_client_id.c_str());
  std::string body = ha_build_refresh_body(clientId, std::string(ha_refresh_token.c_str()));

  String uri = ha_url;
  while (uri.endsWith("/")) uri.remove(uri.length() - 1);
  uri += "/auth/token";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_POST);
  req->addHeader("Content-Type", "application/x-www-form-urlencoded");
  // Safe: send() -> mg_connect_http_opt() copies the body synchronously while `body` is in scope.
  req->setContent((const uint8_t *)body.c_str(), body.length());
  req->onResponse([this](MongooseHttpClientResponse *response) {
    _refreshInFlight = false;
    if (response->respCode() == 200) {
      HaTokens t;
      MongooseString respBody = response->body();
      std::string json((const char *)respBody, respBody.length());
      if (ha_parse_token_response(json, t)) {
        storeTokens(t);
        return;
      }
    }
    DBUGF("[ha] refresh response: %d", response->respCode());
    if (response->respCode() == 400) {
      // invalid_grant: refresh token revoked -> disconnect.
      disconnect();
    }
    // transient errors: leave tokens; loop() retries after backoff.
  });
  _client.send(req);
}

void HomeAssistantClient::get(const String &path, MongooseHttpResponseHandler onResponse) {
  if (!isConnected() || ha_url.length() == 0) return;

  String uri = ha_url;
  while (uri.endsWith("/")) uri.remove(uri.length() - 1);
  uri += path; // caller passes a leading-slash path, e.g. "/api/states/sensor.x"

  String bearer = "Bearer ";
  bearer += ha_access_token;

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_GET);
  req->addHeader("Authorization", bearer.c_str());
  req->addHeader("Accept", "application/json");
  req->onResponse(onResponse);
  _client.send(req);
}

void HomeAssistantClient::pollVehicle() {
  _vehicleInFlight = true;
  _vehiclePollStart = millis();
  pollVehicleField(0);
}

// Fetch one configured vehicle entity, set the matching EVSE value, then chain to
// the next field. Sequential (one in-flight request at a time) to stay safe on the
// shared MongooseHttpClient. An empty entity, a failed request, or an
// unknown/unavailable state simply skips that field (keeps the last good value).
void HomeAssistantClient::pollVehicleField(int field) {
  String entity;
  switch (field) {
    case 0: entity = ha_vehicle_soc;          break;
    case 1: entity = ha_vehicle_range;        break;
    case 2: entity = ha_vehicle_eta;          break;
    case 3: entity = ha_vehicle_charge_limit; break;
    default: _vehicleInFlight = false; return; // chain complete (now at field 4)
  }

  if (entity.length() == 0) {
    pollVehicleField(field + 1); // not configured -> skip to next
    return;
  }

  String path = "/api/states/" + entity; // entity IDs are URL-safe (sensor.x_y)
  int next = field + 1;
  get(path, [this, field, next](MongooseHttpClientResponse *response) {
    if (response->respCode() == 200) {
      MongooseString body = response->body();
      std::string state;
      if (ha_parse_entity_state(std::string((const char *)body, body.length()), state)) {
        char *endp = nullptr;
        double v = strtod(state.c_str(), &endp);
        if (endp == state.c_str()) {
          // non-numeric state (e.g. "charging") -> skip, don't write 0 to the EVSE
          DBUGF("[ha] vehicle field %d: non-numeric state, skipping", field);
        } else {
          switch (field) {
            case 0: evse.setVehicleStateOfCharge((int)lround(v)); break;
            case 1: evse.setVehicleRange((int)lround(v));         break;
            case 2: evse.setVehicleEta((int)lround(v));           break; // seconds
            case 3: evse.setVehicleChargeLimit((int)lround(v));   break;
          }
        }
      } else {
        DBUGF("[ha] vehicle field %d: state unavailable/unparseable", field);
      }
    } else {
      DBUGF("[ha] vehicle field %d: HTTP %d", field, response->respCode());
    }
    pollVehicleField(next); // advance regardless of this field's outcome
  });
}
