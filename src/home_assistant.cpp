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
#define HA_POLL_MS                (30 * 1000UL)   // poll HA entities every 30 s
#define HA_POLL_TIMEOUT_MS        (30 * 1000UL)   // clear a stuck poll chain

enum HaValueType { HA_NUMERIC, HA_BOOL, HA_STRING };

enum HaSink {
  SINK_VEHICLE_SOC = 0,
  SINK_VEHICLE_RANGE,
  SINK_VEHICLE_ETA,
  SINK_VEHICLE_CHARGE_LIMIT,
  SINK_VEHICLE_PLUGGED,
  SINK_VEHICLE_CHARGING_STATE,
  SINK_HOME_BATTERY_SOC,
  SINK_HOME_BATTERY_POWER,
};

struct HaPollEntry {
  const String *entity;   // config string; empty => skip
  bool (*gate)();         // is this row active right now?
  int  type;              // HaValueType
  int  sinkId;            // HaSink
};

static bool gateVehicleHA() {
  return vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT;
}

// Home-battery feeds poll whenever the entity is configured; the loop already
// gates on isConnected(), which implies HA is enabled and authorized.
static bool gateAlways() { return true; }

static const HaPollEntry HA_POLL_TABLE[] = {
  { &ha_vehicle_soc,             gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_SOC },
  { &ha_vehicle_range,           gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_RANGE },
  { &ha_vehicle_eta,             gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_ETA },
  { &ha_vehicle_charge_limit,    gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_CHARGE_LIMIT },
  { &ha_vehicle_plugged,         gateVehicleHA, HA_BOOL,    SINK_VEHICLE_PLUGGED },
  { &ha_vehicle_charging_state,  gateVehicleHA, HA_STRING,  SINK_VEHICLE_CHARGING_STATE },
  { &ha_battery_soc,             gateAlways,    HA_NUMERIC, SINK_HOME_BATTERY_SOC },
  { &ha_battery_power,           gateAlways,    HA_NUMERIC, SINK_HOME_BATTERY_POWER },
};
static const int HA_POLL_TABLE_LEN = sizeof(HA_POLL_TABLE) / sizeof(HA_POLL_TABLE[0]);

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
  _lastPoll(0),
  _pollInFlight(false),
  _pollStart(0)
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

void HomeAssistantClient::addStatusFields(JsonDocument &doc) {
  if (_homeBatterySocValid)   doc["home_battery_soc"] = _homeBatterySoc;
  if (_homeBatteryPowerValid) doc["home_battery_power"] = _homeBatteryPower;
  // Vehicle extras only while HA is the selected vehicle data source -- avoids
  // emitting stale plugged/charging values after the source switches away.
  if (vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT) {
    if (_vehiclePluggedValid)   doc["vehicle_plugged"] = _vehiclePlugged;
    if (_vehicleChargingState.length() > 0)
      doc["vehicle_charging_state"] = _vehicleChargingState;
  }
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
  _homeBatterySocValid = false;
  _homeBatteryPowerValid = false;
  _vehiclePluggedValid = false;
  _vehicleChargingState = "";
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

  // Recover a stuck poll chain (a request that never completed and no onResponse
  // fired) -- otherwise _pollInFlight would block polling forever.
  if (_pollInFlight && _pollStart != 0 &&
      (millis() - _pollStart) > HA_POLL_TIMEOUT_MS) {
    DBUGLN("[ha] poll chain timed out, clearing in-flight flag");
    _pollInFlight = false;
  }

  // Poll HA entities when connected and at least one configured feed selects HA.
  if (isConnected()
      && anyPollActive()
      && !_pollInFlight
      && (_lastPoll == 0 || (millis() - _lastPoll) >= HA_POLL_MS)) {
    _lastPoll = millis();
    if (_lastPoll == 0) _lastPoll = 1; // 0 means "never polled"
    _pollInFlight = true;
    _pollStart = millis();
    pollNext(0);
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

bool HomeAssistantClient::get(const String &path, MongooseHttpResponseHandler onResponse) {
  if (!isConnected() || ha_url.length() == 0) return false;

  // Don't send a token that's at/near expiry -- HA would reject it and log an
  // invalid-auth attempt. Skip the request; loop() refreshes when due.
  if (ha_refresh_due(ha_token_expires, ha_now_unix(), HA_REFRESH_MARGIN_SEC)) {
    DBUGLN("[ha] get() skipped: token refresh due");
    return false;
  }

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
  return true;
}

bool HomeAssistantClient::anyPollActive() {
  for (int i = 0; i < HA_POLL_TABLE_LEN; i++) {
    const HaPollEntry &e = HA_POLL_TABLE[i];
    if (e.entity->length() > 0 && e.gate()) return true;
  }
  return false;
}

// Apply one parsed entity state to its sink, dispatched by sinkId.
void HomeAssistantClient::applyEntity(int sinkId, int type, const String &state) {
  if (type == HA_NUMERIC) {
    char *endp = nullptr;
    double v = strtod(state.c_str(), &endp);
    if (endp == state.c_str()) {
      DBUGF("[ha] sink %d: non-numeric state, skipping", sinkId);
      return;
    }
    switch (sinkId) {
      case SINK_VEHICLE_SOC:          evse.setVehicleStateOfCharge((int)lround(v)); break;
      case SINK_VEHICLE_RANGE:        evse.setVehicleRange((int)lround(v));         break;
      case SINK_VEHICLE_ETA:          evse.setVehicleEta((int)lround(v));           break; // seconds
      case SINK_VEHICLE_CHARGE_LIMIT: evse.setVehicleChargeLimit((int)lround(v));   break;
      case SINK_HOME_BATTERY_SOC:
        _homeBatterySoc = (int)lround(v); _homeBatterySocValid = true; break;
      case SINK_HOME_BATTERY_POWER:
        _homeBatteryPower = (int)lround(v); _homeBatteryPowerValid = true; break;
      default: break;
    }
  } else if (type == HA_BOOL) {
    bool b;
    if (!ha_parse_bool(std::string(state.c_str()), b)) {
      DBUGF("[ha] sink %d: unrecognized bool state, skipping", sinkId);
      return;
    }
    switch (sinkId) {
      case SINK_VEHICLE_PLUGGED: _vehiclePlugged = b; _vehiclePluggedValid = true; break;
      default: break;
    }
  } else if (type == HA_STRING) {
    // ha_parse_entity_state already rejected ""/unavailable/unknown, so `state` is real.
    switch (sinkId) {
      case SINK_VEHICLE_CHARGING_STATE: _vehicleChargingState = state; break;
      default: break;
    }
  } else {
    DBUGF("[ha] sink %d: unknown value type %d (bug)", sinkId, type);
  }
}

// Fetch one active+configured entity, apply it, then chain to the next. Sequential
// (one in-flight request at a time) to stay safe on the shared MongooseHttpClient.
// An empty entity, inactive gate, failed request, or unparseable state simply skips
// that row (keeps the last good value).
void HomeAssistantClient::pollNext(int index) {
  for (int i = index; i < HA_POLL_TABLE_LEN; i++) {
    const HaPollEntry &e = HA_POLL_TABLE[i];
    if (e.entity->length() == 0 || !e.gate()) {
      continue; // not configured / not active -> skip
    }

    String path = "/api/states/" + *e.entity; // entity IDs are URL-safe (sensor.x_y)
    int next = i + 1;
    int sinkId = e.sinkId;
    int type = e.type;
    bool sent = get(path, [this, sinkId, type, next](MongooseHttpClientResponse *response) {
      if (response->respCode() == 200) {
        MongooseString body = response->body();
        std::string state;
        if (ha_parse_entity_state(std::string((const char *)body, body.length()), state)) {
          applyEntity(sinkId, type, String(state.c_str()));
        } else {
          DBUGF("[ha] sink %d: state unavailable/unparseable", sinkId);
        }
      } else {
        DBUGF("[ha] sink %d: HTTP %d", sinkId, response->respCode());
      }
      pollNext(next); // advance regardless of this row's outcome
    });

    if (!sent) {
      // get() declined (refresh due / not connected): onResponse won't fire, so end
      // the chain now rather than stranding _pollInFlight until the timeout.
      _pollInFlight = false;
    }
    return; // one in-flight request at a time; the callback resumes the walk
  }

  // Walked off the end with nothing left to send -> chain complete.
  _pollInFlight = false;
}
