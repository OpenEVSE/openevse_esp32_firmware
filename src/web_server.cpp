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
#include "hal.h"

MongooseHttpServer server;          // Create class for Web server

bool enableCors = true;

// Event timeouts
unsigned long wifiRestartTime = 0;
unsigned long mqttRestartTime = 0;
unsigned long systemRebootTime = 0;
unsigned long apOffTime = 0;

// Content Types
const char _CONTENT_TYPE_HTML[] PROGMEM = "text/html";
const char _CONTENT_TYPE_TEXT[] PROGMEM = "text/text";
const char _CONTENT_TYPE_CSS[] PROGMEM = "text/css";
const char _CONTENT_TYPE_JSON[] PROGMEM = "application/json";
const char _CONTENT_TYPE_JS[] PROGMEM = "application/javascript";
const char _CONTENT_TYPE_JPEG[] PROGMEM = "image/jpeg";
const char _CONTENT_TYPE_PNG[] PROGMEM = "image/png";
const char _CONTENT_TYPE_SVG[] PROGMEM = "image/svg+xml";

static const char _DUMMY_PASSWORD[] PROGMEM = "_DUMMY_PASSWORD";
#define DUMMY_PASSWORD FPSTR(_DUMMY_PASSWORD)

// Get running firmware version from build tag environment variable
#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)
String currentfirmware = ESCAPEQUOTE(BUILD_TAG);

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
  if(qpass.equals(DUMMY_PASSWORD)) {
    qpass = epass;
  }

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

  String apikey = request->getParam("apikey");
  if(apikey.equals(DUMMY_PASSWORD)) {
    apikey = emoncms_apikey;
  }

  config_save_emoncms(isPositive(request->getParam("enable")),
                      request->getParam("server"),
                      request->getParam("node"),
                      apikey,
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
  if(pass.equals(DUMMY_PASSWORD)) {
    pass = mqtt_pass;
  }

  config_save_mqtt(isPositive(request->getParam("enable")),
                   request->getParam("server"),
                   request->getParam("topic"),
                   request->getParam("user"),
                   pass,
                   request->getParam("solar"),
                   request->getParam("grid_ie"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s %s %s", mqtt_server.c_str(),
          mqtt_topic.c_str(), mqtt_user.c_str(), mqtt_pass.c_str(),
          mqtt_solar.c_str(), mqtt_grid_ie.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);

  // If connected disconnect MQTT to trigger re-connect with new details
  mqttRestartTime = millis();
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
  if(qpass.equals(DUMMY_PASSWORD)) {
    qpass = www_password;
  }

  config_save_admin(quser, qpass);

  response->setCode(200);
  response->print("saved");
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

  config_save_advanced(qhostname);

  response->setCode(200);
  response->print("saved");
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

  String s = "{";
  if (net_eth_connected()) {
    s += "\"mode\":\"Wired\",";
  } else if (net_wifi_mode_is_sta_only()) {
    s += "\"mode\":\"STA\",";
  } else if (net_wifi_mode_is_ap_only()) {
    s += "\"mode\":\"AP\",";
  } else if (net_wifi_mode_is_ap() && net_wifi_mode_is_sta()) {
    s += "\"mode\":\"STA+AP\",";
  }

  s += "\"wifi_client_connected\":" + String(net_wifi_client_connected()) + ",";
  s += "\"eth_connected\":" + String(net_eth_connected()) + ",";
  s += "\"net_connected\":" + String(net_is_connected()) + ",";
  s += "\"srssi\":" + String(WiFi.RSSI()) + ",";
  s += "\"ipaddress\":\"" + ipaddress + "\",";

  s += "\"emoncms_connected\":" + String(emoncms_connected) + ",";
  s += "\"packets_sent\":" + String(packets_sent) + ",";
  s += "\"packets_success\":" + String(packets_success) + ",";

  s += "\"mqtt_connected\":" + String(mqtt_connected()) + ",";

  s += "\"ohm_hour\":\"" + ohm_hour + "\",";

  s += "\"free_heap\":" + String(HAL.getFreeHeap()) + ",";

  s += "\"comm_sent\":" + String(rapiSender.getSent()) + ",";
  s += "\"comm_success\":" + String(rapiSender.getSuccess()) + ",";
  s += "\"rapi_connected\":" + String(rapiSender.isConnected()) + ",";

  s += "\"amp\":" + String(amp) + ",";
  s += "\"pilot\":" + String(pilot) + ",";
  s += "\"temp1\":" + String(temp1) + ",";
  s += "\"temp2\":" + String(temp2) + ",";
  s += "\"temp3\":" + String(temp3) + ",";
  s += "\"state\":" + String(state) + ",";
  s += "\"elapsed\":" + String(elapsed) + ",";
  s += "\"wattsec\":" + String(wattsec) + ",";
  s += "\"watthour\":" + String(watthour_total) + ",";

  s += "\"gfcicount\":" + String(gfci_count) + ",";
  s += "\"nogndcount\":" + String(nognd_count) + ",";
  s += "\"stuckcount\":" + String(stuck_count) + ",";

  s += "\"divertmode\":" + String(divertmode) + ",";
  s += "\"solar\":" + String(solar) + ",";
  s += "\"grid_ie\":" + String(grid_ie) + ",";
  s += "\"charge_rate\":" + String(charge_rate) + ",";
  s += "\"divert_update\":" + String((millis() - lastUpdate) / 1000) + ",";

  s += "\"ota_update\":" + String(Update.isRunning());

#ifdef ENABLE_LEGACY_API
  s += ",\"networks\":[" + st + "]";
  s += ",\"rssi\":[" + rssi + "]";
  s += ",\"version\":\"" + currentfirmware + "\"";
  s += ",\"ssid\":\"" + esid + "\"";
  //s += ",\"pass\":\""+epass+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"emoncms_server\":\"" + emoncms_server + "\"";
  s += ",\"emoncms_node\":\"" + emoncms_node + "\"";
  //s += ",\"emoncms_apikey\":\""+emoncms_apikey+"\""; security risk: DONT RETURN APIKEY
  s += ",\"emoncms_fingerprint\":\"" + emoncms_fingerprint + "\"";
  s += ",\"mqtt_server\":\"" + mqtt_server + "\"";
  s += ",\"mqtt_topic\":\"" + mqtt_topic + "\"";
  s += ",\"mqtt_user\":\"" + mqtt_user + "\"";
  //s += ",\"mqtt_pass\":\""+mqtt_pass+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"www_username\":\"" + www_username + "\"";
  //s += ",\"www_password\":\""+www_password+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"ohmkey\":\"" + ohm + "\"";
#endif
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}

// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfig(MongooseHttpServerRequest *request) {
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String dummyPassword = String(DUMMY_PASSWORD);

  String s = "{";
  s += "\"firmware\":\"" + firmware + "\",";
  s += "\"protocol\":\"" + protocol + "\",";
  s += "\"espflash\":" + String(HAL.getFlashChipSize()) + ",";
  s += "\"version\":\"" + currentfirmware + "\",";
  s += "\"diodet\":" + String(diode_ck) + ",";
  s += "\"gfcit\":" + String(gfci_test) + ",";
  s += "\"groundt\":" + String(ground_ck) + ",";
  s += "\"relayt\":" + String(stuck_relay) + ",";
  s += "\"ventt\":" + String(vent_ck) + ",";
  s += "\"tempt\":" + String(temp_ck) + ",";
  s += "\"service\":" + String(service) + ",";
#ifdef ENABLE_LEGACY_API
  s += "\"l1min\":\"" + current_l1min + "\",";
  s += "\"l1max\":\"" + current_l1max + "\",";
  s += "\"l2min\":\"" + current_l2min + "\",";
  s += "\"l2max\":\"" + current_l2max + "\",";
  s += "\"kwhlimit\":\"" + kwh_limit + "\",";
  s += "\"timelimit\":\"" + time_limit + "\",";
  s += "\"gfcicount\":" + String(gfci_count) + ",";
  s += "\"nogndcount\":" + String(nognd_count) + ",";
  s += "\"stuckcount\":" + String(stuck_count) + ",";
#endif
  s += "\"scale\":" + String(current_scale) + ",";
  s += "\"offset\":" + String(current_offset) + ",";
  s += "\"ssid\":\"" + esid + "\",";
  s += "\"pass\":\"";
  if(epass != 0) {
    s += dummyPassword;
  }
  s += "\",";
  s += "\"emoncms_enabled\":" + String(config_emoncms_enabled() ? "true" : "false") + ",";
  s += "\"emoncms_server\":\"" + emoncms_server + "\",";
  s += "\"emoncms_node\":\"" + emoncms_node + "\",";
  s += "\"emoncms_apikey\":\"";
  if(emoncms_apikey != 0) {
    s += dummyPassword;
  }
  s += "\",";
  s += "\"emoncms_fingerprint\":\"" + emoncms_fingerprint + "\",";
  s += "\"mqtt_enabled\":" + String(config_mqtt_enabled() ? "true" : "false") + ",";
  s += "\"mqtt_server\":\"" + mqtt_server + "\",";
  s += "\"mqtt_topic\":\"" + mqtt_topic + "\",";
  s += "\"mqtt_user\":\"" + mqtt_user + "\",";
  s += "\"mqtt_pass\":\"";
  if(mqtt_pass != 0) {
    s += dummyPassword;
  }
  s += "\",";
  s += "\"mqtt_solar\":\""+mqtt_solar+"\",";
  s += "\"mqtt_grid_ie\":\""+mqtt_grid_ie+"\",";
  s += "\"mqtt_supported_protocols\":[\"mqtt\"],";
  s += "\"http_supported_protocols\":[\"http\",\"https\"],";
  s += "\"www_username\":\"" + www_username + "\",";
  s += "\"www_password\":\"";
  if(www_password != 0) {
    s += dummyPassword;
  }
  s += "\",";
  s += "\"hostname\":\"" + esp_hostname + "\",";
  s += "\"ohm_enabled\":" + String(config_ohm_enabled() ? "true" : "false");
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}

#ifdef ENABLE_LEGACY_API
// -------------------------------------------------------------------
// Returns Updates JSON
// url: /rapiupdate
// -------------------------------------------------------------------
void
handleUpdate(MongooseHttpServerRequest *request) {

  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  s += "\"comm_sent\":" + String(rapiSender.getSent()) + ",";
  s += "\"comm_success\":" + String(rapiSender.getSuccess()) + ",";
  s += "\"ohmhour\":\"" + ohm_hour + "\",";
  s += "\"espfree\":\"" + String(espfree) + "\",";
  s += "\"packets_sent\":\"" + String(packets_sent) + "\",";
  s += "\"packets_success\":\"" + String(packets_success) + "\",";
  s += "\"amp\":" + amp + ",";
  s += "\"pilot\":" + pilot + ",";
  s += "\"temp1\":" + temp1 + ",";
  s += "\"temp2\":" + temp2 + ",";
  s += "\"temp3\":" + temp3 + ",";
  s += "\"state\":" + String(state) + ",";
  s += "\"elapsed\":" + String(elapsed) + ",";
  s += "\"estate\":\"" + estate + "\",";
  s += "\"wattsec\":" + wattsec + ",";
  s += "\"watthour\":" + watthour_total;
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}
#endif

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
  HAL.eraseConfig();

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

    lcd_display(F("Updating WiFi"), 0, 0, 0, LCD_CLEAR_LINE);
    lcd_display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE);
    lcd_loop();

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
        lcd_display(text, 0, 1, 10 * 1000, LCD_DISPLAY_NOW);
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
      lcd_display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
      upgradeResponse->setCode(200);
      upgradeResponse->print("OK");
      request->send(upgradeResponse);
      upgradeResponse = NULL;
    } else {
      DBUGF("Update failed: %d", Update.getError());
      lcd_display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
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
  char jsonString[8];
  int jsonFound = request->getParam("json", jsonString, sizeof(jsonString));
  bool json = jsonFound >= 0 && (0 == jsonFound || isPositive(String(jsonString)));

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
  server.on("/saveadvanced$", handleSaveAdvanced);
  server.on("/saveohmkey$", handleSaveOhmkey);
  server.on("/reset$", handleRst);
  server.on("/restart$", handleRestart);
  server.on("/rapi$", handleRapi);
  server.on("/r$", handleRapi);
  server.on("/scan$", handleScan);
  server.on("/apoff$", handleAPOff);
  server.on("/divertmode$", handleDivertMode);
  server.on("/emoncms/describe$", handleDescribe);

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

  server.on("/ws$")->onFrame(onWsFrame);

  server.onNotFound(handleNotFound);

  DEBUG.println("Server started");
}

void
web_server_loop() {
  Profile_Start(web_server_loop);

  // Do we need to restart the WiFi?
  if((wifiRestartTime > 0 && millis() > wifiRestartTime)) {
    wifiRestartTime = 0;
    net_wifi_restart();
  }
  else if (WiFi.status() != WL_CONNECTED)
  {
    if (wifiRestartTime == 0)
    	wifiRestartTime = millis() + 2000;
  }
  else
    wifiRestartTime = 0;

  // Do we need to restart MQTT?
  if(mqttRestartTime > 0 && millis() > mqttRestartTime) {
    mqttRestartTime = 0;
    mqtt_restart();
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
    HAL.reset();
  }

  Profile_End(web_server_loop, 5);
}

void web_server_event(String &event)
{
  server.sendAll(event);
}
