#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <Update.h>
#include "certificates.h"

#include <string>

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
#include "ocpp.h"
#include "input.h"
#include "emoncms.h"
#include "divert.h"
#include "lcd.h"
#include "espal.h"
#include "time_man.h"
#include "tesla_client.h"
#include "scheduler.h"
#include "rfid.h"
#include "current_shaper.h"
#include "evse_man.h"
#include "limit.h"

MongooseHttpServer server;          // Create class for Web server
MongooseHttpServer redirect;        // Server to redirect to HTTPS if enabled

bool enableCors = false;
bool streamDebug = false;

// Event timeouts
static unsigned long wifiRestartTime = 0;
static unsigned long apOffTime = 0;

// Content Types
const char _CONTENT_TYPE_HTML[]     PROGMEM = "text/html";
const char _CONTENT_TYPE_TEXT[]     PROGMEM = "text/plain";
const char _CONTENT_TYPE_CSS[]      PROGMEM = "text/css";
const char _CONTENT_TYPE_JSON[]     PROGMEM = "application/json";
const char _CONTENT_TYPE_JS[]       PROGMEM = "text/javascript";
const char _CONTENT_TYPE_JPEG[]     PROGMEM = "image/jpeg";
const char _CONTENT_TYPE_PNG[]      PROGMEM = "image/png";
const char _CONTENT_TYPE_SVG[]      PROGMEM = "image/svg+xml";
const char _CONTENT_TYPE_ICO[]      PROGMEM = "image/vnd.microsoft.icon";
const char _CONTENT_TYPE_WOFF[]     PROGMEM = "font/woff";
const char _CONTENT_TYPE_WOFF2[]    PROGMEM = "font/woff2";
const char _CONTENT_TYPE_MANIFEST[] PROGMEM = "application/manifest+json";

#define RAPI_RESPONSE_BLOCKED             -300

void handleConfig(MongooseHttpServerRequest *request);
void handleEvseClaimsTarget(MongooseHttpServerRequest *request);
void handleEvseClaims(MongooseHttpServerRequest *request);
void handleEventLogs(MongooseHttpServerRequest *request);
void handleCertificates(MongooseHttpServerRequest *request);

void handleUpdateRequest(MongooseHttpServerRequest *request);
size_t handleUpdateUpload(MongooseHttpServerRequest *request, int ev, MongooseString filename, uint64_t index, uint8_t *data, size_t len);
void handleUpdateClose(MongooseHttpServerRequest *request);

void handleTime(MongooseHttpServerRequest *request);
void handleTimePost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response);

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
bool requestPreProcess(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *&response, fstr_t contentType)
{
  dumpRequest(request);

  if(!net.isWifiModeApOnly() && www_username!="" &&
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

//---------------------------------------------------------------------
// Build status data
// --------------------------------------------------------------------

void buildStatus(DynamicJsonDocument &doc) {

  // Get the current time
  struct timeval local_time;
  gettimeofday(&local_time, NULL);

  struct tm * timeinfo = gmtime(&local_time.tv_sec);

  char time[64];
  char offset[8];
  strftime(time, sizeof(time), "%FT%TZ", timeinfo);
  strftime(offset, sizeof(offset), "%z", timeinfo);

  if (net.isWiredConnected()) {
    doc["mode"] = "Wired";
  } else if (net.isWifiModeStaOnly()) {
    doc["mode"] = "STA";
  } else if (net.isWifiModeApOnly()) {
    doc["mode"] = "AP";
  } else if (net.isWifiModeAp() && net.isWifiModeSta()) {
    doc["mode"] = "STA+AP";
  }

  doc["wifi_client_connected"] = (int)net.isWifiClientConnected();
  doc["eth_connected"] = (int)net.isWiredConnected();
  doc["net_connected"] = (int)net.isWifiClientConnected();
  doc["ipaddress"] = net.getIp();
  doc["macaddress"] = net.getMac();

  doc["emoncms_connected"] = (int)emoncms_connected;
  doc["packets_sent"] = packets_sent;
  doc["packets_success"] = packets_success;

  doc["mqtt_connected"] = (int)mqtt_connected();

  doc["ocpp_connected"] = (int)OcppTask::isConnected();

#if defined(ENABLE_PN532) || defined(ENABLE_RFID)
  doc["rfid_failure"] = (int) rfid.communicationFails();
#endif

  doc["ohm_hour"] = ohm_hour;

  doc["free_heap"] = ESPAL.getFreeHeap();

  doc["comm_sent"] = rapiSender.getSent();
  doc["comm_success"] = rapiSender.getSuccess();
  doc["rapi_connected"] = (int)rapiSender.isConnected();
  doc["evse_connected"] = (int)evse.isConnected();

  create_rapi_json(doc);

  doc["gfcicount"] = evse.getFaultCountGFCI();
  doc["nogndcount"] = evse.getFaultCountNoGround();
  doc["stuckcount"] = evse.getFaultCountStuckRelay();

  doc["solar"] = solar;
  doc["grid_ie"] = grid_ie;
  doc["charge_rate"] = divert.getChargeRate();
  doc["divert_update"] = (millis() - divert.getLastUpdate()) / 1000;
  doc["divert_active"] = divert.isActive();
  doc["shaper"] = shaper.getState()?1:0;
  doc["shaper_live_pwr"] = shaper.getLivePwr();
  // doc["shaper_cur"] = shaper.getChgCur();
  doc["shaper_cur"] = shaper.getMaxCur();
  doc["shaper_updated"] = shaper.isUpdated();
  doc["service_level"] = static_cast<uint8_t>(evse.getActualServiceLevel());
  doc["limit"] = limit.hasLimit();

  doc["ota_update"] = (int)Update.isRunning();

  doc["config_version"] = config_version();
  doc["claims_version"] = evse.getClaimsVersion();
  doc["override_version"] = manual.getVersion();
  doc["schedule_version"] = scheduler.getVersion();
  doc["schedule_plan_version"] = scheduler.getPlanVersion();
  doc["limit_version"] = limit.getVersion();

  doc["vehicle_state_update"] = (millis() - evse.getVehicleLastUpdated()) / 1000;
  if(teslaClient.getVehicleCnt() > 0) {
    doc["tesla_vehicle_count"] = teslaClient.getVehicleCnt();
    doc["tesla_vehicle_id"] = teslaClient.getVehicleId(teslaClient.getCurVehicleIdx());
    doc["tesla_vehicle_name"] = teslaClient.getVehicleDisplayName(teslaClient.getCurVehicleIdx());
    teslaClient.getChargeInfoJson(doc);
  } else {
    doc["tesla_vehicle_count"] = false;
    doc["tesla_vehicle_id"] = false;
    doc["tesla_vehicle_name"] = false;
    if(evse.isVehicleStateOfChargeValid()) {
      doc["battery_level"] = evse.getVehicleStateOfCharge();
    }
    if(evse.isVehicleRangeValid()) {
      doc["battery_range"] = evse.getVehicleRange();
    }
    if(evse.isVehicleEtaValid()) {
      doc["time_to_full_charge"] = evse.getVehicleEta();
    }
  }

  DBUGF("/status ArduinoJson size: %dbytes", doc.size());
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

  DBUGF("Starting WiFi scan");
  net.wifiScanNetworks([request, response](int networksFound) {
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
      json += "}";
    }
    json += "]";
    response->print(json);
    request->send(response);
  });
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
// Change divert mode (solar PV divert mode) e.g 1:Normal (default), 2:Eco
// url: /divertmode
// -------------------------------------------------------------------
void
handleDivertMode(MongooseHttpServerRequest *request){
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  DivertMode divertmode = (DivertMode)(request->getParam("divertmode").toInt());
  divert.setMode(divertmode);

  response->setCode(200);
  response->print("Divert Mode changed");
  request->send(response);

  DBUGF("Divert Mode: %d", divertmode);
}

// -------------------------------------------------------------------
// Change current shaper mode 0:disable (default), 1:Enable
// url: /shaper
// -------------------------------------------------------------------
void
handleCurrentShaper(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }
  shaper.setState(request->getParam("shaper").toInt() == 1? true: false);

  response->setCode(200);
  response->print("Current Shaper state changed");
  request->send(response);

  DBUGF("CurrentShaper: %d", shaper.getState());
}

// -------------------------------------------------------------------
// Manually set the time
// url: /settime
// -------------------------------------------------------------------
void handleSetTime(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  handleTimePost(request, response);

  request->send(response);
}

// -------------------------------------------------------------------
// Get Tesla Vehicle Info
// url: /teslaveh
// -------------------------------------------------------------------
void
handleTeslaVeh(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  StaticJsonDocument<1024> doc;
  int count = teslaClient.getVehicleCnt();
  doc["count"] = count;
  JsonArray vehicles = doc.createNestedArray("vehicles");

  for (int i = 0; i < count; i++)
  {
    JsonObject vehicle = vehicles.createNestedObject();
    vehicle["id"] = teslaClient.getVehicleId(i);
    vehicle["name"] = teslaClient.getVehicleDisplayName(i);
  }

  response->setCode(200);
  serializeJson(doc, *response);
  request->send(response);
}

// -------------------------------------------------------------------
// Returns status json
// url: /status
// -------------------------------------------------------------------
void handleStatusPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();
  // Deserialize the JSON document
  const size_t capacity = JSON_OBJECT_SIZE(32) + 1024;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, body);
  if(!error)
  {
    bool send_event = true;

    if(doc.containsKey("voltage"))
    {
      double volts = doc["voltage"];
      DBUGF("voltage:%.1f", volts);
      evse.setVoltage(volts);
    }
    if(doc.containsKey("shaper_live_pwr"))
    {
      double shaper_live_pwr = doc["shaper_live_pwr"];
      shaper.setLivePwr(shaper_live_pwr);
      DBUGF("shaper: live power:%dW", shaper.getLivePwr());
    }
    if(doc.containsKey("solar")) {
      solar = doc["solar"];
      DBUGF("solar:%dW", solar);
      divert.update_state();
      // recalculate shaper
      if (shaper.getState()) {
        shaper.shapeCurrent();
      }
      send_event = false; // Divert sends the event so no need to send here
    }
    else if(doc.containsKey("grid_ie")) {
      grid_ie = doc["grid_ie"];
      DBUGF("grid:%dW", grid_ie);
      divert.update_state();
      // recalculate shaper
      if (shaper.getState()) {
        shaper.shapeCurrent();
      }
      send_event = false; // Divert sends the event so no need to send here
    }
    if(doc.containsKey("battery_level") && vehicle_data_src == VEHICLE_DATA_SRC_HTTP) {
      double vehicle_soc = doc["battery_level"];
      DBUGF("vehicle_soc:%d%%", vehicle_soc);
      evse.setVehicleStateOfCharge(vehicle_soc);
      doc["vehicle_state_update"] = 0;
    }
    if(doc.containsKey("battery_range") && vehicle_data_src == VEHICLE_DATA_SRC_HTTP) {
      double vehicle_range = doc["battery_range"];
      DBUGF("vehicle_range:%dKM", vehicle_range);
      evse.setVehicleRange(vehicle_range);
      doc["vehicle_state_update"] = 0;
    }
    if(doc.containsKey("time_to_full_charge") && vehicle_data_src == VEHICLE_DATA_SRC_HTTP){
      double vehicle_eta = doc["time_to_full_charge"];
      DBUGF("vehicle_eta:%d", vehicle_eta);
      evse.setVehicleEta(vehicle_eta);
      doc["vehicle_state_update"] = 0;
    }
    // send back new value to clients
    if(send_event) {
      event_send(doc);
    }
    response->setCode(200);
    serializeJson(doc, *response);
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Could not parse JSON\"}");
  }
}


void
handleStatus(MongooseHttpServerRequest *request)
{

  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {

    const size_t capacity = JSON_OBJECT_SIZE(128) + 2048;
    DynamicJsonDocument doc(capacity);
    buildStatus(doc);
    response->setCode(200);
    serializeJson(doc, *response);

  } else if(HTTP_POST == request->method()) {
    handleStatusPost(request, response);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
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
  if(SCHEDULER_EVENT_NULL != event)
  {
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

//----------------------------------------------------------
//
//            Limit
//
//----------------------------------------------------------

void handleLimitGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  if(limit.hasLimit())
  {
    limit.get().serialize(response);
  } else {
    response->setCode(200);
    response->print("{}");
  }
}

void handleLimitPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();

    if (limit.set(body)) {
      response->setCode(201);
      response->print("{\"msg\":\"done\"}");
      // todo: mqtt_publish_limit();  // update limit props to mqtt
    } else {
      // unused for now
      response->setCode(400);
      response->print("{\"msg\":\"failed to parse JSON\"}");
    }
}

void handleLimitDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  if(limit.hasLimit()) {
    if (limit.clear()) {
      response->setCode(200);
      response->print("{\"msg\":\"done\"}");
    } else {
      response->setCode(500);
      response->print("{\"msg\":\"failed\"}");
    }
  } else {
    response->setCode(200);
    response->print("{\"msg\":\"no limit\"}");
  }
}

void handleLimit(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleLimitGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleLimitPost(request, response);
  } else if(HTTP_DELETE == request->method()) {
    handleLimitDelete(request, response);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}

//----------------------------------------------------------
//
//            Energy Meter
//
//----------------------------------------------------------
void handleEmeterDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, body);
  if (DeserializationError::Code::Ok == err) {
    if (doc.containsKey("hard") && doc.containsKey("import")) {
      bool hardreset = (bool)doc["hard"];
      bool import = (bool)doc["import"];
      if (evse.resetEnergyMeter(hardreset,import)) {
        response->setCode(200);
        response->print("{\"msg\":\"Reset done\"}");
      }
      else {
        response->setCode(500);
        response->print("{\"msg\":\"Reset failed\"}");
      }

    }
    else {
      response->setCode(500);
      response->print("{\"msg\":\"Reset Failed\"}");
    }
  }
  else {
    response->setCode(500);
    response->print("{\"msg\":\"reset Failed\"}");
  }
}

void handleEmeter(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response))
  {
    return;
  }

  if (HTTP_DELETE == request->method())
  {
    handleEmeterDelete(request, response);
  }
  else
  {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}



  //----------------------------------------------------------

  void handleOverrideGet(MongooseHttpServerRequest * request, MongooseHttpServerResponseStream * response)
  {
    if (manual.isActive())
    {
      EvseProperties props;
      manual.getProperties(props);
      props.serialize(response);
    } else {
    response->setCode(200);
    response->print("{}");
  }
}

void handleOverridePost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();

  EvseProperties props;
  if(props.deserialize(body))
  {
    if(manual.claim(props)) {
      response->setCode(201);
      response->print("{\"msg\":\"Created\"}");
      mqtt_publish_override();  // update override state to mqtt
    } else {
      response->setCode(500);
      response->print("{\"msg\":\"Failed to claim manual overide\"}");
    }
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"Failed to parse JSON\"}");
  }
}

void handleOverrideDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  if(manual.release()) {
    response->setCode(200);
    response->print("{\"msg\":\"Deleted\"}");
    mqtt_publish_override();  // update override state to mqtt
  } else {
    response->setCode(500);
    response->print("{\"msg\":\"Failed to release manual overide\"}");
  }
}

void handleOverridePatch(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  if(manual.toggle())
  {
    response->setCode(200);
    response->print("{\"msg\":\"Updated\"}");
    mqtt_publish_override();  // update override state to mqtt
  } else {
    response->setCode(500);
    response->print("{\"msg\":\"Failed to toggle manual overide\"}");
  }
}

void
handleOverride(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleOverrideGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleOverridePost(request, response);
  } else if(HTTP_DELETE == request->method()) {
    handleOverrideDelete(request, response);
  } else if(HTTP_PATCH == request->method()) {
    handleOverridePatch(request, response);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

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

  restart_system();
}


// -------------------------------------------------------------------
// Restart (Reboot gateway or evse)
// url: /restart
// -------------------------------------------------------------------
void
handleRestart(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  if (HTTP_GET == request->method()) {
    response->setCode(200);
    response->print("1");
    request->send(response);
    restart_system();
  }
  else if (HTTP_POST == request->method()) {
    String body = request->body().toString();
    // Deserialize the JSON document
    const size_t capacity = JSON_OBJECT_SIZE(1) + 16;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, body);
    if(!error)
    {
      if(doc.containsKey("device")){
        if (strcmp(doc["device"], "gateway") == 0 ) {
          response->setCode(200);
          response->print("{\"msg\":\"restart gateway\"}");
          request->send(response);
          restart_system();
        }
        else if (strcmp(doc["device"], "evse") == 0) {
          response->setCode(200);
          response->print("{\"msg\":\"restart evse\"}");
          request->send(response);
          evse.restartEvse();
        }
        else {
          response->setCode(404);
          response->print("{\"msg\":\"unknown device\"}");
          request->send(response);
        }
      }
      else {
        response->setCode(400);
        response->print("{\"msg\":\"wrong payload\"}");
        request->send(response);
      }
    } else {
      response->setCode(400);
      response->print("{\"msg\":\"Couldn't parse json\"}");
      request->send(response);
    }
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
    request->send(response);
  }
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
  if(false == requestPreProcess(request, response)) {
    return;
  }
  response->setCode(200);
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->print("{\"msg\":\"Waiting for badge\"}");
  request->send(response);
  yield();
  rfid.waitForTag();
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
    int ret = RAPI_RESPONSE_NK;

    if(!evse.isRapiCommandBlocked(rapi))
    {
      // BUG: Really we should do this in the main loop not here...
      RAPI_PORT.flush();
      DBUGVAR(rapi);
      ret = rapiSender.sendCmdSync(rapi);
      DBUGVAR(ret);
    } else {
      ret = RAPI_RESPONSE_BLOCKED;
    }

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
        RAPI_RESPONSE_BLOCKED == ret ? F("RAPI_RESPONSE_BLOCKED") :
        F("UNKNOWN");

      if (json) {
        s = "{\"cmd\":\""+rapi+"\",\"error\":\""+errorString+"\"}";
      } else {
        s += rapi;
        s += F("<p><strong>Error:</strong>");
        s += errorString;
      }

      code = RAPI_RESPONSE_BLOCKED == ret ? 400 : 500;
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

  if(net.isWifiModeAp()) {
    // Redirect to the home page in AP mode (for the captive portal)
    MongooseHttpServerResponseStream *response = request->beginResponseStream();
    response->setContentType(CONTENT_TYPE_HTML);

    String url = F("http://");
    url += net.getIp();

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

void handleHttpsRedirect(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response = request->beginResponseStream();
  response->setContentType(CONTENT_TYPE_HTML);

  String url = F("https://");
  url += request->host().toString();
  url += request->uri().toString();

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
}

void onWsFrame(MongooseHttpWebSocketConnection *connection, int flags, uint8_t *data, size_t len)
{
  DBUGF("Got message %.*s", len, (const char *)data);
  const size_t capacity = JSON_OBJECT_SIZE(1) + 16;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, data, len);
  if (!error) {
    if (doc.containsKey("ping") && doc["ping"].is<int8_t>())
      {
        // answer pong
        connection->send("{\"pong\": 1}");

      }
  }
}

void onWsConnect(MongooseHttpWebSocketConnection *connection)
{
  DBUGF("New client connected over ws");
  // pushing states to client
  const size_t capacity = JSON_OBJECT_SIZE(40) + 1024;
  DynamicJsonDocument doc(capacity);
  buildStatus(doc);
  web_server_event(doc);
}

/*
 * Really simple 'conversion' of ASCII to UTF-8, basically only a few places send >127 chars
 * so just filter those to be acceptable as UTF-8
 */
void web_server_send_ascii_utf8(const char *endpoint, const uint8_t *buffer, size_t size)
{
  char temp[size];
  for(int i = 0; i < size; i++) {
    temp[i] = buffer[i] & 0x7f;
  }
  server.sendAll(endpoint, WEBSOCKET_OP_TEXT, temp, size);
}

void web_server_setup()
{
  bool use_ssl = false;
  if(www_certificate_id != "")
  {
    uint64_t cert_id = std::stoull(www_certificate_id.c_str(), nullptr, 16);
    const char *cert = certs.getCertificate(cert_id);
    const char *key = certs.getKey(cert_id);
    if(NULL != cert && NULL != key)
    {
      server.begin(443, cert, key);
      use_ssl = true;

      redirect.begin(80);
      redirect.on("/", handleHttpsRedirect);
    }
  }

  if(false == use_ssl) {
    server.begin(80);
  }

  // Handle status updates
  server.on("/status$", handleStatus);
  server.on("/config$", handleConfig);

  // Handle HTTP web interface button presses
  server.on("/teslaveh$", handleTeslaVeh);
  server.on("/tesla/vehicles$", handleTeslaVeh);
  server.on("/settime$", handleSetTime);
  server.on("/reset$", handleRst);
  server.on("/restart$", handleRestart);
  server.on("/rapi$", handleRapi);
  server.on("/r$", handleRapi);
  server.on("/scan$", handleScan);
  server.on("/apoff$", handleAPOff);
  server.on("/divertmode$", handleDivertMode);
  server.on("/shaper$", handleCurrentShaper);
  server.on("/emoncms/describe$", handleDescribe);
  server.on("/rfid/add$", handleAddRFID);

  server.on("/schedule/plan$", handleSchedulePlan);
  server.on("/schedule", handleSchedule);

  server.on("/claims/target$", handleEvseClaimsTarget);
  server.on("/claims", handleEvseClaims);

  server.on("/override$", handleOverride);

  server.on("/logs", handleEventLogs);
  server.on("/certificates", handleCertificates);
  server.on("/limit", handleLimit);
  server.on("/emeter", handleEmeter);
  server.on("/time", handleTime);

  // Simple Firmware Update Form
  server.on("/update$")->
    onRequest(handleUpdateRequest)->
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
    web_server_send_ascii_utf8("/evse/console", buffer, size);
  });
  SerialEvse.onRead([](const uint8_t *buffer, size_t size) {
    web_server_send_ascii_utf8("/evse/console", buffer, size);
  });

  server.on("/ws$")->
    onFrame(onWsFrame)
    ->
    onConnect(onWsConnect);

  server.onNotFound(handleNotFound);

  DEBUG.println("Server started");
}

void
web_server_loop() {
  Profile_Start(web_server_loop);

  // Do we need to restart the WiFi?
  if(wifiRestartTime > 0 && millis() > wifiRestartTime) {
    wifiRestartTime = 0;
    net.wifiRestart();
  }

  // Do we need to turn off the access point?
  if(apOffTime > 0 && millis() > apOffTime) {
    apOffTime = 0;
    net.wifiTurnOffAp();
  }

  Profile_End(web_server_loop, 5);
}

void web_server_event(JsonDocument &event)
{
  String json;
  serializeJson(event, json);
  server.sendAll("/ws", json);
}
