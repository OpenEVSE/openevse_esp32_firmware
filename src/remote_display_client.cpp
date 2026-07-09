// src/remote_display_client.cpp — see remote_display_client.h.
#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_REMOTE_DISPLAY)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_REMOTE_DISPLAY_CLIENT

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <MicroTasks.h>

#include "emonesp.h"      // DBUG + the sensor scale factors the station serialises with
#include "app_config.h"   // remote_display_host
#include "net_manager.h"
#include "openevse.h"     // OPENEVSE_STATE_CHARGING
#include "remote_display_client.h"

// mDNS answers get cached this long before re-querying (they also refresh
// whenever remote_display_host changes).
#define MDNS_CACHE_MS (5 * 60 * 1000)

RemoteDisplayClient::RemoteDisplayClient() :
  MicroTasks::Task()
{
}

void RemoteDisplayClient::begin()
{
  MicroTask.startTask(this);
}

void RemoteDisplayClient::setup()
{
}

// Turn remote_display_host into something Mongoose can connect to. Plain IPs
// and DNS names pass through; "*.local" names are resolved here via mDNS
// (Mongoose's resolver only speaks unicast DNS) and cached.
bool RemoteDisplayClient::resolveHost(String &host)
{
  host = remote_display_host;
  host.trim();
  if(host.length() == 0) {
    return false; // not configured yet
  }

  if(!host.endsWith(".local")) {
    return true;
  }

  if(_resolvedFrom == host && _resolvedHost.length() > 0 &&
     (uint32_t)(millis() - _resolvedAt) < MDNS_CACHE_MS)
  {
    host = _resolvedHost;
    return true;
  }

  String name = host.substring(0, host.length() - strlen(".local"));
  IPAddress ip = MDNS.queryHost(name.c_str()); // blocks up to ~2 s
  if(ip == IPAddress()) {
    DBUGF("RemoteDisplay: mDNS lookup failed for %s", host.c_str());
    return false;
  }

  _resolvedFrom = host;
  _resolvedHost = ip.toString();
  _resolvedAt = millis();
  DBUGF("RemoteDisplay: %s -> %s", host.c_str(), _resolvedHost.c_str());
  host = _resolvedHost;
  return true;
}

void RemoteDisplayClient::parseStatus(const char *body, size_t len)
{
  // Only pull the keys the screens need out of the (large) /status document.
  StaticJsonDocument<384> filter;
  filter["state"] = true;
  filter["vehicle"] = true;
  filter["amp"] = true;
  filter["voltage"] = true;
  filter["power"] = true;
  filter["pilot"] = true;
  filter["temp"] = true;
  filter["session_elapsed"] = true;
  filter["elapsed"] = true;          // pre-session_elapsed stations
  filter["session_energy"] = true;
  filter["total_energy"] = true;
  filter["total_day"] = true;

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, body, len, DeserializationOption::Filter(filter));
  if(err) {
    DBUGF("RemoteDisplay: /status parse failed: %s", err.c_str());
    return;
  }
  if(!doc.containsKey("state")) {
    DBUGLN("RemoteDisplay: /status missing state, ignoring");
    return;
  }

  _state   = doc["state"].as<uint8_t>();
  _vehicle = doc["vehicle"].as<int>() != 0;
  // Normalise back to EvseManager units: the station multiplies by the legacy
  // scale factors when serialising (emonesp.h: amp in mA, temp in deci-degC).
  _amps    = doc["amp"].as<double>() / AMPS_SCALE_FACTOR;
  _voltage = doc["voltage"].as<double>() / VOLTS_SCALE_FACTOR;
  _power   = doc["power"].as<double>() / POWER_SCALE_FACTOR;
  _pilot   = doc["pilot"].as<long>();
  _temp_valid = doc["temp"].is<float>(); // serialised as false when invalid
  if(_temp_valid) {
    _temp = doc["temp"].as<double>() / TEMP_SCALE_FACTOR;
  }
  _elapsed = doc.containsKey("session_elapsed") ? doc["session_elapsed"].as<uint32_t>()
                                                : doc["elapsed"].as<uint32_t>();
  _session_wh = doc["session_energy"].as<double>();  // Wh
  _total_kwh  = doc["total_energy"].as<double>();    // kWh
  _day_kwh    = doc["total_day"].as<double>();       // kWh

  _lastRx = millis();

  DBUGF("RemoteDisplay: state=%u vehicle=%d %.1fV %.1fA pilot=%ldA %.0fW elapsed=%u",
        _state, _vehicle ? 1 : 0, _voltage, _amps, _pilot, _power, _elapsed);
}

uint32_t RemoteDisplayClient::getSessionElapsed()
{
  if(!isDataValid()) {
    return 0;
  }
  uint32_t elapsed = _elapsed;
  if(OPENEVSE_STATE_CHARGING == _state) {
    // Tick locally between polls so the ELAPSED tile doesn't jump in 5 s steps.
    elapsed += (uint32_t)(millis() - _lastRx) / 1000;
  }
  return elapsed;
}

unsigned long RemoteDisplayClient::loop(MicroTasks::WakeReason reason)
{
  if(!net.isConnected()) {
    return 1000;
  }

  if(_requestPending)
  {
    // onClose clears the flag on every normal path (including connect
    // failures); this timeout is just a backstop so a wedged socket can't
    // stall polling forever.
    if((uint32_t)(millis() - _requestStarted) < REMOTE_DISPLAY_HTTP_TIMEOUT_MS) {
      return 250;
    }
    DBUGLN("RemoteDisplay: request timed out");
    _requestPending = false;
  }

  String host;
  if(!resolveHost(host)) {
    return REMOTE_DISPLAY_POLL_MS;
  }

  String url = String("http://") + host + "/status";
  DBUGF("RemoteDisplay: GET %s", url.c_str());
  _requestPending = true;
  _requestStarted = millis();
  _client.get(url,
    [this](MongooseHttpClientResponse *response)
    {
      if(200 == response->respCode()) {
        String body = response->body().toString();
        parseStatus(body.c_str(), body.length());
      } else {
        DBUGF("RemoteDisplay: HTTP %d from station", response->respCode());
      }
    },
    [this]()
    {
      _requestPending = false;
    });

  return REMOTE_DISPLAY_POLL_MS;
}

RemoteDisplayClient remoteDisplay;

#endif // ENABLE_REMOTE_DISPLAY_CLIENT
