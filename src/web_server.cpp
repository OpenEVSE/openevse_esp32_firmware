#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <Update.h>

typedef const __FlashStringHelper *fstr_t;

#ifdef ESP32

#include <WiFi.h>

#elif defined(ESP8266)

#include <ESP8266WiFi.h>

#else
#error Platform not supported
#endif

//#include <FS.h>                       // SPIFFS file-system: store web server html, CSS etc.

#include "emonesp.h"
#include "web_server.h"
#include "web_server_static.h"
#include "app_config.h"
#include "net_manager.h"
#include "mqtt.h"
#include "input.h"
#include "emoncms.h"
#include "divert.h"
#include "lcd.h"
#include "espal.h"
#include "time_man.h"
#include "tesla_client.h"
#include "scheduler.h"
#include "rfid.h"

MongooseHttpServer server;          // Create class for Web server

bool enableCors = true;
bool streamDebug = false;

// Event timeouts
static unsigned long wifiRestartTime = 0;
static unsigned long systemRebootTime = 0;
static unsigned long apOffTime = 0;

// Content Types
const char _CONTENT_TYPE_HTML[] PROGMEM = "text/html";
const char _CONTENT_TYPE_TEXT[] PROGMEM = "text/plain";
const char _CONTENT_TYPE_CSS[] PROGMEM = "text/css";
const char _CONTENT_TYPE_JSON[] PROGMEM = "application/json";
const char _CONTENT_TYPE_JS[] PROGMEM = "application/javascript";
const char _CONTENT_TYPE_JPEG[] PROGMEM = "image/jpeg";
const char _CONTENT_TYPE_PNG[] PROGMEM = "image/png";
const char _CONTENT_TYPE_SVG[] PROGMEM = "image/svg+xml";

void dumpRequest(MongooseHttpServerRequest *request)
{
#ifdef ENABLE_DEBUG_WEB_REQUEST
  DBUGF("host.length = %d", request->host().length());
  DBUGF("host.c_str = %p", request->host().c_str());
  DBUGF("uri.length = %d", request->uri().length());
  DBUGF("uri.c_str = %p", request->uri().c_str());

  if(request->method() == HTTP_GET) {
    DBUG("GET");
  } else if(request->method() == HTTP_POST) {
    DBUG("POST");
  } else if(request->method() == HTTP_DELETE) {
    DBUG("DELETE");
  } else if(request->method() == HTTP_PUT) {
    DBUG("PUT");
  } else if(request->method() == HTTP_PATCH) {
    DBUG("PATCH");
  } else if(request->method() == HTTP_HEAD) {
    DBUG("HEAD");
  } else if(request->method() == HTTP_OPTIONS) {
    DBUG("OPTIONS");
  } else {
    DBUG("UNKNOWN");
  }
  DBUGF(" http://%.*s%.*s",
    request->host().length(), request->host().c_str(),
    request->uri().length(), request->uri().c_str());

  if(request->contentLength()){
    DBUGF("_CONTENT_TYPE: %.*s", request->contentType().length(), request->contentType().c_str());
    DBUGF("_CONTENT_LENGTH: %u", request->contentLength());
  }

  int headers = request->headers();
  int i;
  for(i=0; i<headers; i++) {
    DBUGF("_HEADER[%.*s]: %.*s",
      request->headerNames(i).length(), request->headerNames(i).c_str(),
      request->headerValues(i).length(), request->headerValues(i).c_str());
  }

  /*
  int params = request->params();
  for(i = 0; i < params; i++) {
    AsyncWebParameter* p = request->getParam(i);
    if(p->isFile()){
      DBUGF("_FILE[%s]: %s, size: %u", p->name().c_str(), p->value().c_str(), p->size());
    } else if(p->isPost()){
      DBUGF("_POST[%s]: %s", p->name().c_str(), p->value().c_str());
    } else {
      DBUGF("_GET[%s]: %s", p->name().c_str(), p->value().c_str());
    }
  }
  */
#endif
}

// -------------------------------------------------------------------
// Helper function to perform the standard operations on a request
// -------------------------------------------------------------------
bool requestPreProcess(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *&response, fstr_t contentType = CONTENT_TYPE_JSON)
{
  dumpRequest(request);

  if(!net_wifi_mode_is_ap_only() && www_username!="" &&
     false == request->authenticate(www_username, www_password)) {
    request->requestAuthentication(esp_hostname);
    return false;
  }

  response = request->beginResponseStream();
  response->setContentType(contentType);

  if(enableCors) {
    response->addHeader(F("Access-Control-Allow-Origin"), F("*"));
    response->addHeader(F("Access-Control-Allow-Headers"), F("*"));
    response->addHeader(F("Access-Control-Allow-Methods"), F("*"));
  }

  response->addHeader(F("Cache-Control"), F("no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0"));

  return true;
}

// -------------------------------------------------------------------
// Helper function to detect positive string
// -------------------------------------------------------------------
bool isPositive(const String &str) {
  return str == "1" || str == "true";
}

bool isPositive(MongooseHttpServerRequest *request, const char *param) {
  char paramValue[8];
  int paramFound = request->getParam(param, paramValue, sizeof(paramValue));
  return paramFound >= 0 && (0 == paramFound || isPositive(String(paramValue)));
}

// -------------------------------------------------------------------
// Wifi scan /scan not currently used
// url: /scan
//
// First request will return 0 results unless you start scan from somewhere else (loop/setup)
// Do not request more often than 3-5 seconds
// -------------------------------------------------------------------
void
handleScan(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }

#ifndef ENABLE_ASYNC_WIFI_SCAN
  String json = "[";
  int n = WiFi.scanComplete();
  if(n == -2) {
    WiFi.scanNetworks(true, false);
  } else if(n) {
    for (int i = 0; i < n; ++i) {
      if(i) json += ",";
      json += "{";
      json += "\"rssi\":"+String(WiFi.RSSI(i));
      json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
      json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
      json += ",\"channel\":"+String(WiFi.channel(i));
      json += ",\"secure\":"+String(WiFi.encryptionType(i));
#ifndef ESP32
      json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
#endif // !ESP32
      json += "}";
    }
    WiFi.scanDelete();
    if(WiFi.scanComplete() == -2){
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  response->print(json);
  request->send(response);
#else // ENABLE_ASYNC_WIFI_SCAN
  // Async WiFi scan need the Git version of the ESP8266 core
  if(WIFI_SCAN_RUNNING == WiFi.scanComplete()) {
    response->setCode(500);
    response->setContentType(CONTENT_TYPE_TEXT);
    response->print("Busy");
    request->send(response);
    return;
  }

  DBUGF("Starting WiFi scan");
  WiFi.scanNetworksAsync([request, response](int networksFound) {
    DBUGF("%d networks found", networksFound);
    String json = "[";
    for (int i = 0; i < networksFound; ++i) {
      if(i) json += ",";
      json += "{";
      json += "\"rssi\":"+String(WiFi.RSSI(i));
      json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
      json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
      json += ",\"channel\":"+String(WiFi.channel(i));
      json += ",\"secure\":"+String(WiFi.encryptionType(i));
      json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
      json += "}";
    }
    WiFi.scanDelete();
    json += "]";
    response->print(json);
    request->send(response);
  }, false);
#endif
}

// -------------------------------------------------------------------
// Handle turning Access point off
// url: /apoff
// -------------------------------------------------------------------
void
handleAPOff(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  response->setCode(200);
  response->print("Turning AP Off");
  request->send(response);

  DBUGLN("Turning AP Off");
  apOffTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Save selected network to EEPROM and attempt connection
// url: /savenetwork
// -------------------------------------------------------------------
void
handleSaveNetwork(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String qsid = request->getParam("ssid");
  String qpass = request->getParam("pass");
  if (qsid != 0) {
    config_save_wifi(qsid, qpass);

    response->setCode(200);
    response->print("saved");
    wifiRestartTime = millis() + 2000;
  } else {
    response->setCode(400);
    response->print("No SSID");
  }

  request->send(response);
}

// -------------------------------------------------------------------
// Save Emoncms
// url: /saveemoncms
// -------------------------------------------------------------------
void
handleSaveEmoncms(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  config_save_emoncms(isPositive(request->getParam("enable")),
                      request->getParam("server"),
                      request->getParam("node"),
                      request->getParam("apikey"),
                      request->getParam("fingerprint"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s",
           emoncms_server.c_str(),
           emoncms_node.c_str(),
           emoncms_apikey.c_str(),
           emoncms_fingerprint.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);
}

// -------------------------------------------------------------------
// Save MQTT Config
// url: /savemqtt
// -------------------------------------------------------------------
void
handleSaveMqtt(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String pass = request->getParam("pass");

  int protocol = MQTT_PROTOCOL_MQTT;
  char proto[6];
  if(request->getParam("protocol", proto, sizeof(proto)) > 0) {
    // Cheap and chearful check, obviously not checking for invalid input
    protocol = 's' == proto[4] ? MQTT_PROTOCOL_MQTT_SSL : MQTT_PROTOCOL_MQTT;
  }

  int port = 1883;
  char portStr[8];
  if(request->getParam("port", portStr, sizeof(portStr)) > 0) {
    port = atoi(portStr);
  }

  char unauthString[8];
  int unauthFound = request->getParam("reject_unauthorized", unauthString, sizeof(unauthString));
  bool reject_unauthorized = unauthFound < 0 || 0 == unauthFound || isPositive(String(unauthString));

  config_save_mqtt(isPositive(request->getParam("enable")),
                   protocol,
                   request->getParam("server"),
                   port,
                   request->getParam("topic"),
                   request->getParam("user"),
                   pass,
                   request->getParam("solar"),
                   request->getParam("grid_ie"),
                   reject_unauthorized);

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s %s %s", mqtt_server.c_str(),
          mqtt_topic.c_str(), mqtt_user.c_str(), mqtt_pass.c_str(),
          mqtt_solar.c_str(), mqtt_grid_ie.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);

  // If connected disconnect MQTT to trigger re-connect with new details
  mqtt_restart();
}

// -------------------------------------------------------------------
// Change divert mode (solar PV divert mode) e.g 1:Normal (default), 2:Eco
// url: /divertmode
// -------------------------------------------------------------------
void
handleDivertMode(MongooseHttpServerRequest *request){
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  divertmode_update(request->getParam("divertmode").toInt());

  response->setCode(200);
  response->print("Divert Mode changed");
  request->send(response);

  DBUGF("Divert Mode: %d", divertmode);
}

// -------------------------------------------------------------------
// Save the web site user/pass
// url: /saveadmin
// -------------------------------------------------------------------
void
handleSaveAdmin(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String quser = request->getParam("user");
  String qpass = request->getParam("pass");

  config_save_admin(quser, qpass);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Manually set the time
// url: /settime
// -------------------------------------------------------------------
void
handleSetTime(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  bool qsntp_enable = isPositive(request, "ntp");
  String qtz = request->getParam("tz");

  config_save_sntp(qsntp_enable, qtz);
  if(config_sntp_enabled()) {
    time_check_now();
  }

  if(false == qsntp_enable)
  {
    String time = request->getParam("time");

    struct tm tm;

    int yr, mnth, d, h, m, s;
    if(6 == sscanf( time.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &yr, &mnth, &d, &h, &m, &s))
    {
      tm.tm_year = yr - 1900;
      tm.tm_mon = mnth - 1;
      tm.tm_mday = d;
      tm.tm_hour = h;
      tm.tm_min = m;
      tm.tm_sec = s;

      struct timeval set_time = {0,0};
      set_time.tv_sec = mktime(&tm);

      time_set_time(set_time, "manual");

    }
    else
    {
      response->setCode(400);
      response->print("could not parse time");
      request->send(response);
      return;
    }
  }

  response->setCode(200);
  response->print("set");
  request->send(response);
}

// -------------------------------------------------------------------
// Save advanced settings
// url: /saveadvanced
// -------------------------------------------------------------------
void
handleSaveAdvanced(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String qhostname = request->getParam("hostname");
  String qsntp_host = request->getParam("sntp_host");

  config_save_advanced(qhostname, qsntp_host);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Get Tesla Vehicle Info
// url: /teslaveh
// -------------------------------------------------------------------
void
handleTeslaVeh(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  int vc = teslaClient.getVehicleCnt();
  s += "\"count:\"" + String(vc);
  if (vc) {
    s += ",[";
    for (int i=0;i < vc;i++) {
      s += "{\"id\":\"" + teslaClient.getVehicleId(i) + "\",";
      s += "\"name\":\"" + teslaClient.getVehicleDisplayName(i) + "\"}";
      if (i < vc-1) s += ",";
    }
    s += "]";
  }
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}
// -------------------------------------------------------------------
// Save the Ohm keyto EEPROM
// url: /handleSaveOhmkey
// -------------------------------------------------------------------
void
handleSaveOhmkey(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  bool enabled = isPositive(request->getParam("enable"));
  String qohm = request->getParam("ohm");

  config_save_ohm(enabled, qohm);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Returns status json
// url: /status
// -------------------------------------------------------------------
void
handleStatus(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  // Get the current time
  struct timeval local_time;
  gettimeofday(&local_time, NULL);

  struct tm * timeinfo = gmtime(&local_time.tv_sec);

  char time[64];
  char offset[8];
  strftime(time, sizeof(time), "%FT%TZ", timeinfo);
  strftime(offset, sizeof(offset), "%z", timeinfo);

  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);

  if (net_eth_connected()) {
    doc["mode"] = "Wired";
  } else if (net_wifi_mode_is_sta_only()) {
    doc["mode"] = "STA";
  } else if (net_wifi_mode_is_ap_only()) {
    doc["mode"] = "AP";
  } else if (net_wifi_mode_is_ap() && net_wifi_mode_is_sta()) {
    doc["mode"] = "STA+AP";
  }

  doc["wifi_client_connected"] = (int)net_wifi_client_connected();
  doc["eth_connected"] = (int)net_eth_connected();
  doc["net_connected"] = (int)net_is_connected();
  doc["ipaddress"] = ipaddress;

  doc["emoncms_connected"] = (int)emoncms_connected;
  doc["packets_sent"] = packets_sent;
  doc["packets_success"] = packets_success;

  doc["mqtt_connected"] = (int)mqtt_connected();

  doc["ohm_hour"] = ohm_hour;

  doc["free_heap"] = ESPAL.getFreeHeap();

  doc["comm_sent"] = rapiSender.getSent();
  doc["comm_success"] = rapiSender.getSuccess();
  doc["rapi_connected"] = (int)rapiSender.isConnected();
  doc["evse_connected"] = (int)evse.isConnected();

  create_rapi_json(doc);

  doc["elapsed"] = evse.getSessionElapsed();
  doc["wattsec"] = evse.getSessionEnergy() * SESSION_ENERGY_SCALE_FACTOR;
  doc["watthour"] = evse.getTotalEnergy() * TOTAL_ENERGY_SCALE_FACTOR;

  doc["gfcicount"] = evse.getFaultCountGFCI();
  doc["nogndcount"] = evse.getFaultCountNoGround();
  doc["stuckcount"] = evse.getFaultCountStuckRelay();

  doc["solar"] = solar;
  doc["grid_ie"] = grid_ie;
  doc["charge_rate"] = charge_rate;
  doc["divert_update"] = (millis() - lastUpdate) / 1000;

  doc["ota_update"] = (int)Update.isRunning();
  doc["time"] = String(time);
  doc["offset"] = String(offset);

  {
    const TESLA_CHARGE_INFO *tci = teslaClient.getChargeInfo();
    if (tci->isValid) {
      doc["batteryRange"] = String(tci->batteryRange) + ",";
      doc["chargeEnergyAdded"] = String(tci->chargeEnergyAdded) + ",";
      doc["chargeMilesAddedRated"] = String(tci->chargeMilesAddedRated) + ",";
      doc["batteryLevel"] = String(tci->batteryLevel) + ",";
      doc["chargeLimitSOC"] = String(tci->chargeLimitSOC) + ",";
      doc["timeToFullCharge"] = String(tci->timeToFullCharge) + ",";
      doc["chargerVoltage"] = String(tci->chargerVoltage);
    }
  }

  response->setCode(200);
  serializeJson(doc, *response);
  request->send(response);
}

// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfigGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);

  // EVSE Config
  doc["firmware"] = evse.getFirmwareVersion();
  doc["protocol"] = "-";
  doc["espflash"] = ESPAL.getFlashChipSize();
  doc["version"] = currentfirmware;
  doc["diodet"] = evse.isDiodeCheckDisabled() ? 1 : 0;
  doc["gfcit"] = evse.isGfiTestDisabled() ? 1 : 0;
  doc["groundt"] = evse.isGroundCheckDisabled() ? 1 : 0;
  doc["relayt"] = evse.isStuckRelayCheckDisabled() ? 1 : 0;
  doc["ventt"] = evse.isVentRequiredDisabled() ? 1 : 0;
  doc["tempt"] = evse.isTemperatureCheckDisabled() ? 1 : 0;
  doc["service"] = static_cast<uint8_t>(evse.getServiceLevel());
  doc["scale"] = current_scale;
  doc["offset"] = current_offset;

  // Static supported protocols
  JsonArray mqtt_supported_protocols = doc.createNestedArray("mqtt_supported_protocols");
  mqtt_supported_protocols.add("mqtt");
  mqtt_supported_protocols.add("mqtts");

  JsonArray http_supported_protocols = doc.createNestedArray("http_supported_protocols");
  http_supported_protocols.add("http");
  http_supported_protocols.add("https");

  config_serialize(doc, true, false, true);

  response->setCode(200);
  serializeJson(doc, *response);
}

void
handleConfigPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();
  if(config_deserialize(body)) {
    config_commit();
    response->setCode(200);
    response->print("{\"msg\":\"done\"}");
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Could not parse JSON\"}");
  }
}

void
handleConfig(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleConfigGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleConfigPost(request, response);
  } else if(HTTP_OPTIONS == request->method()) {
    response->setCode(200);
  } else {
    response->setCode(405);

  }

  request->send(response);
}

// -------------------------------------------------------------------
//
// url: /schedule
// -------------------------------------------------------------------
void
handleScheduleGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint16_t event)
{
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);

  bool success = (SCHEDULER_EVENT_NULL == event) ?
    scheduler.serialize(doc) :
    scheduler.serialize(doc, event);

  if(success) {
    response->setCode(200);
    serializeJson(doc, *response);
  } else {
    response->setCode(404);
    response->print("{\"msg\":\"Not found\"}");
  }
}

void
handleSchedulePost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint16_t event)
{
  String body = request->body().toString();

  bool success = (SCHEDULER_EVENT_NULL == event) ?
    scheduler.deserialize(body) :
    scheduler.deserialize(body, event);

  if(success) {
    response->setCode(200);
    response->print("{\"msg\":\"done\"}");
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Could not parse JSON\"}");
  }
}

void
handleScheduleDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, uint16_t event)
{
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);

  if(SCHEDULER_EVENT_NULL != event) {
    if(scheduler.removeEvent(event)) {
      response->setCode(200);
      response->print("{\"msg\":\"done\"}");
    } else {
      response->setCode(404);
      response->print("{\"msg\":\"Not found\"}");
    }
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }
}

#define SCHEDULE_PATH_LEN (sizeof("/schedule/") - 1)

void
handleSchedule(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  uint16_t event = SCHEDULER_EVENT_NULL;

  String path = request->uri();
  if(path.length() > SCHEDULE_PATH_LEN) {
    String eventStr = path.substring(SCHEDULE_PATH_LEN);
    DBUGVAR(eventStr);
    event = eventStr.toInt();
  }

  DBUGVAR(event);

  if(HTTP_GET == request->method()) {
    handleScheduleGet(request, response, event);
  } else if(HTTP_POST == request->method()) {
    handleSchedulePost(request, response, event);
  } else if(HTTP_DELETE == request->method()) {
    handleScheduleDelete(request, response, event);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}

void
handleSchedulePlan(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  const size_t capacity = JSON_OBJECT_SIZE(40) + 2048;
  DynamicJsonDocument doc(capacity);

  scheduler.serializePlan(doc);
  response->setCode(200);
  serializeJson(doc, *response);

  request->send(response);
}

// -------------------------------------------------------------------
// Reset config and reboot
// url: /reset
// -------------------------------------------------------------------
void
handleRst(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  config_reset();
  ESPAL.eraseConfig();

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRebootTime = millis() + 1000;
}


// -------------------------------------------------------------------
// Restart (Reboot)
// url: /restart
// -------------------------------------------------------------------
void
handleRestart(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRebootTime = millis() + 1000;
}


// -------------------------------------------------------------------
// Emoncms describe end point,
// Allows local device discover using https://github.com/emoncms/find
// url: //emoncms/describe
// -------------------------------------------------------------------
void handleDescribe(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseBasic *response = request->beginResponse();
  response->setCode(200);
  response->setContentType(CONTENT_TYPE_TEXT);
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->setContent("openevse");
  request->send(response);
}

void handleAddRFID(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }
  response->setCode(200);
  response->setContentType(CONTENT_TYPE_TEXT);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
  rfid.waitForTag(60);
}

void handlePollRFID(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return;
  }
  response->setCode(200);
  response->setContentType(CONTENT_TYPE_JSON);
  response->addHeader("Access-Control-Allow-Origin", "*");
  serializeJson(rfid.rfidPoll(), *response);
  request->send(response);
}

// -------------------------------------------------------------------
// Update firmware
// url: /update
// -------------------------------------------------------------------
void
handleUpdateGet(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_HTML)) {
    return;
  }

  response->setCode(200);
  response->print(
    F("<html><form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware'> "
        "<input type='submit' value='Update'>"
      "</form></html>"));
  request->send(response);
}

static MongooseHttpServerResponseStream *upgradeResponse = NULL;

void
handleUpdatePost(MongooseHttpServerRequest *request) {
  if(NULL != upgradeResponse) {
    request->send(500, CONTENT_TYPE_TEXT, "Error: Upgrade in progress");
    return;
  }

  if(false == requestPreProcess(request, upgradeResponse, CONTENT_TYPE_TEXT)) {
    return;
  }

  // TODO: Add support for returning 100: Continue
}

static int lastPercent = -1;

static void handleUpdateError(MongooseHttpServerRequest *request)
{
  upgradeResponse->setCode(500);
  upgradeResponse->printf("Error: %d", Update.getError());
  request->send(upgradeResponse);
  upgradeResponse = NULL;

  // Anoyingly this uses Stream rather than Print...
#ifdef ENABLE_DEBUG
  Update.printError(DEBUG_PORT);
#endif
}

size_t
handleUpdateUpload(MongooseHttpServerRequest *request, int ev, MongooseString filename, uint64_t index, uint8_t *data, size_t len)
{
  if(MG_EV_HTTP_PART_BEGIN == ev)
  {
//    dumpRequest(request);

    DEBUG_PORT.printf("Update Start: %s\n", filename.c_str());

    lcd.display(F("Updating WiFi"), 0, 0, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);

    if(!Update.begin()) {
      handleUpdateError(request);
    }
  }

  if(!Update.hasError())
  {
    DBUGF("Update Writing %llu", index);

    size_t contentLength = request->contentLength();
    DBUGVAR(contentLength);
    if(contentLength > 0)
    {
      int percent = index / (contentLength / 100);
      DBUGVAR(percent);
      DBUGVAR(lastPercent);
      if(percent != lastPercent) {
        String text = String(percent) + F("%");
        lcd.display(text, 0, 1, 10 * 1000, LCD_DISPLAY_NOW);
        DEBUG_PORT.printf("Update: %d%%\n", percent);
        lastPercent = percent;
      }
    }
    if(Update.write(data, len) != len) {
      handleUpdateError(request);
    }
  }

  if(MG_EV_HTTP_PART_END == ev)
  {
    DBUGLN("Upload finished");
    if(Update.end(true)) {
      DBUGF("Update Success: %lluB", index+len);
      lcd.display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
      upgradeResponse->setCode(200);
      upgradeResponse->print("OK");
      request->send(upgradeResponse);
      upgradeResponse = NULL;
    } else {
      DBUGF("Update failed: %d", Update.getError());
      lcd.display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
      handleUpdateError(request);
    }
  }

  return len;
}

static void handleUpdateClose(MongooseHttpServerRequest *request)
{
  DBUGLN("Update close");

  if(upgradeResponse) {
    delete upgradeResponse;
    upgradeResponse = NULL;
  }

  if(Update.isFinished() && !Update.hasError()) {
    systemRebootTime = millis() + 1000;
  }
}

String delayTimer = "0 0 0 0";

void
handleRapi(MongooseHttpServerRequest *request) {
  bool json = isPositive(request, "json");

  int code = 200;

  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, json ? CONTENT_TYPE_JSON : CONTENT_TYPE_HTML)) {
    return;
  }

  String s;

  if(false == json) {
    s = F("<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p>"
          "<b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>"
          "Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>"
          "Get Real-time Current - $GG<p>Get Temperatures - $GP<p>"
          "<p>"
          "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label>"
          "<input id='rapi' name='rapi' length=32><p><input type='submit'></form>");
  }

  if (request->hasParam("rapi"))
  {
    String rapi = request->getParam("rapi");

    // BUG: Really we should do this in the main loop not here...
    RAPI_PORT.flush();
    DBUGVAR(rapi);
    int ret = rapiSender.sendCmdSync(rapi);
    DBUGVAR(ret);

    if(RAPI_RESPONSE_OK == ret ||
       RAPI_RESPONSE_NK == ret)
    {
      String rapiString = rapiSender.getResponse();

      // Fake $GD if not supported by firmware
      if(RAPI_RESPONSE_OK == ret && rapi.startsWith(F("$ST"))) {
        delayTimer = rapi.substring(4);
      }
      if(RAPI_RESPONSE_NK == ret)
      {
        if(rapi.equals(F("$GD"))) {
          ret = 0;
          rapiString = F("$OK ");
          rapiString += delayTimer;
        }
        else if (rapi.startsWith(F("$FF")))
        {
          DBUGF("Attempting legacy FF support");

          String fallback = F("$S");
          fallback += rapi.substring(4);

          DBUGF("Attempting %s", fallback.c_str());

          int ret = rapiSender.sendCmdSync(fallback.c_str());
          if(RAPI_RESPONSE_OK == ret)
          {
            String rapiString = rapiSender.getResponse();
          }
        }
      }

      if (json) {
        s = "{\"cmd\":\""+rapi+"\",\"ret\":\""+rapiString+"\"}";
      } else {
        s += rapi;
        s += F("<p>&gt;");
        s += rapiString;
      }
    }
    else
    {
      String errorString =
        RAPI_RESPONSE_QUEUE_FULL == ret ? F("RAPI_RESPONSE_QUEUE_FULL") :
        RAPI_RESPONSE_BUFFER_OVERFLOW == ret ? F("RAPI_RESPONSE_BUFFER_OVERFLOW") :
        RAPI_RESPONSE_TIMEOUT == ret ? F("RAPI_RESPONSE_TIMEOUT") :
        RAPI_RESPONSE_OK == ret ? F("RAPI_RESPONSE_OK") :
        RAPI_RESPONSE_NK == ret ? F("RAPI_RESPONSE_NK") :
        RAPI_RESPONSE_INVALID_RESPONSE == ret ? F("RAPI_RESPONSE_INVALID_RESPONSE") :
        RAPI_RESPONSE_CMD_TOO_LONG == ret ? F("RAPI_RESPONSE_CMD_TOO_LONG") :
        RAPI_RESPONSE_BAD_CHECKSUM == ret ? F("RAPI_RESPONSE_BAD_CHECKSUM") :
        RAPI_RESPONSE_BAD_SEQUENCE_ID == ret ? F("RAPI_RESPONSE_BAD_SEQUENCE_ID") :
        RAPI_RESPONSE_ASYNC_EVENT == ret ? F("RAPI_RESPONSE_ASYNC_EVENT") :
        F("UNKNOWN");

      if (json) {
        s = "{\"cmd\":\""+rapi+"\",\"error\":\""+errorString+"\"}";
      } else {
        s += rapi;
        s += F("<p><strong>Error:</strong>");
        s += errorString;
      }

      code = 500;
    }
  }
  if (false == json) {
    s += F("<script type='text/javascript'>document.getElementById('rapi').focus();</script>");
    s += F("<p></html>\r\n\r\n");
  }

  response->setCode(code);
  response->print(s);
  request->send(response);
}

void handleNotFound(MongooseHttpServerRequest *request)
{
  // Not a dynamic handler, check the static resources
  if(web_static_handle(request)) {
    return;
  }

  DBUG("NOT_FOUND: ");
  dumpRequest(request);

  if(net_wifi_mode_is_ap_only()) {
    // Redirect to the home page in AP mode (for the captive portal)
    MongooseHttpServerResponseStream *response = request->beginResponseStream();
    response->setContentType(CONTENT_TYPE_HTML);

    String url = F("http://");
    url += ipaddress;

    String s = F("<html>");
    s += F("<head><meta http-equiv=\"Refresh\" content=\"0; url=");
    s += url;
    s += F("\" /></head><body><a href=\"");
    s += url;
    s += F("\">OpenEVSE</a></body></html>");

    response->setCode(301);
    response->addHeader(F("Location"), url);
    response->print(s);
    request->send(response);
  } else {
    request->send(404);
  }
}

void onWsFrame(MongooseHttpWebSocketConnection *connection, int flags, uint8_t *data, size_t len)
{
  DBUGF("Got message %.*s", len, (const char *)data);
}

void
web_server_setup() {
//  SPIFFS.begin(); // mount the fs

  server.begin(80);

  // Handle status updates
  server.on("/status$", handleStatus);
  server.on("/config$", handleConfig);

  // Handle HTTP web interface button presses
  server.on("/savenetwork$", handleSaveNetwork);
  server.on("/saveemoncms$", handleSaveEmoncms);
  server.on("/savemqtt$", handleSaveMqtt);
  server.on("/saveadmin$", handleSaveAdmin);
  server.on("/teslaveh$", handleTeslaVeh);
  server.on("/saveadvanced$", handleSaveAdvanced);
  server.on("/saveohmkey$", handleSaveOhmkey);
  server.on("/settime$", handleSetTime);
  server.on("/reset$", handleRst);
  server.on("/restart$", handleRestart);
  server.on("/rapi$", handleRapi);
  server.on("/r$", handleRapi);
  server.on("/scan$", handleScan);
  server.on("/apoff$", handleAPOff);
  server.on("/divertmode$", handleDivertMode);
  server.on("/emoncms/describe$", handleDescribe);
  server.on("/rfid/add$", handleAddRFID);

  // Check status of RFID scan
  server.on("/rfid/poll$", handlePollRFID);

  server.on("/schedule/plan$", handleSchedulePlan);
  server.on("/schedule", handleSchedule);

  // Simple Firmware Update Form
  server.on("/update$")->
    onRequest([](MongooseHttpServerRequest *request) {
      if(HTTP_GET == request->method()) {
        handleUpdateGet(request);
      } else if(HTTP_POST == request->method()) {
        handleUpdatePost(request);
      }
    })->
    onUpload(handleUpdateUpload)->
    onClose(handleUpdateClose);

  server.on("/debug$", [](MongooseHttpServerRequest *request) {
    MongooseHttpServerResponseStream *response;
    if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
      return;
    }

    response->setCode(200);
    response->setContentType(CONTENT_TYPE_TEXT);
    response->addHeader("Access-Control-Allow-Origin", "*");
    SerialDebug.printBuffer(*response);
    request->send(response);
  });

  server.on("/debug/console$")->onFrame([](MongooseHttpWebSocketConnection *connection, int flags, uint8_t *data, size_t len) {
  });

  SerialDebug.onWrite([](const uint8_t *buffer, size_t size)
  {
    server.sendAll("/debug/console", WEBSOCKET_OP_TEXT, buffer, size);
  });

  server.on("/evse$", [](MongooseHttpServerRequest *request) {
    MongooseHttpServerResponseStream *response;
    if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
      return;
    }

    response->setCode(200);
    response->setContentType(CONTENT_TYPE_TEXT);
    response->addHeader("Access-Control-Allow-Origin", "*");
    SerialEvse.printBuffer(*response);
    request->send(response);
  });

  server.on("/evse/console$")->onFrame([](MongooseHttpWebSocketConnection *connection, int flags, uint8_t *data, size_t len) {
  });

  SerialEvse.onWrite([](const uint8_t *buffer, size_t size) {
    server.sendAll("/evse/console", WEBSOCKET_OP_TEXT, buffer, size);
  });
  SerialEvse.onRead([](const uint8_t *buffer, size_t size) {
    server.sendAll("/evse/console", WEBSOCKET_OP_TEXT, buffer, size);
  });

  server.on("/ws$")->onFrame(onWsFrame);

  server.onNotFound(handleNotFound);

  DEBUG.println("Server started");
}

void
web_server_loop() {
  Profile_Start(web_server_loop);

  // Do we need to restart the WiFi?
  if(wifiRestartTime > 0 && millis() > wifiRestartTime) {
    wifiRestartTime = 0;
    net_wifi_restart();
  }

  // Do we need to turn off the access point?
  if(apOffTime > 0 && millis() > apOffTime) {
    apOffTime = 0;
    net_wifi_turn_off_ap();
  }

  // Do we need to reboot the system?
  if(systemRebootTime > 0 && millis() > systemRebootTime) {
    systemRebootTime = 0;
    net_wifi_disconnect();
    ESPAL.reset();
  }

  Profile_End(web_server_loop, 5);
}

void web_server_event(JsonDocument &event)
{
  String json;
  serializeJson(event, json);
  server.sendAll("/ws", json);
}
