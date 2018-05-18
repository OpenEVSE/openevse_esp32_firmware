#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <ESP8266WiFi.h>
//#include <FS.h>                       // SPIFFS file-system: store web server html, CSS etc.

#include "emonesp.h"
#include "web_server.h"
#include "web_server_static.h"
#include "config.h"
#include "wifi.h"
#include "mqtt.h"
#include "input.h"
#include "emoncms.h"
#include "divert.h"
#include "lcd.h"

AsyncWebServer server(80);          // Create class for Web server
AsyncWebSocket ws("/ws");
StaticFileWebHandler staticFile;

bool enableCors = true;

// Event timeouts
unsigned long wifiRestartTime = 0;
unsigned long mqttRestartTime = 0;
unsigned long systemRestartTime = 0;
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

static const char _DUMMY_PASSWORD[] PROGMEM = "___DUMMY_PASSWORD___";
#define DUMMY_PASSWORD FPSTR(_DUMMY_PASSWORD)

// Get running firmware version from build tag environment variable
#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)
String currentfirmware = ESCAPEQUOTE(BUILD_TAG);

void dumpRequest(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_GET) {
    DBUGF("GET");
  } else if(request->method() == HTTP_POST) {
    DBUGF("POST");
  } else if(request->method() == HTTP_DELETE) {
    DBUGF("DELETE");
  } else if(request->method() == HTTP_PUT) {
    DBUGF("PUT");
  } else if(request->method() == HTTP_PATCH) {
    DBUGF("PATCH");
  } else if(request->method() == HTTP_HEAD) {
    DBUGF("HEAD");
  } else if(request->method() == HTTP_OPTIONS) {
    DBUGF("OPTIONS");
  } else {
    DBUGF("UNKNOWN");
  }
  DBUGF(" http://%s%s", request->host().c_str(), request->url().c_str());

  if(request->contentLength()){
    DBUGF("_CONTENT_TYPE: %s", request->contentType().c_str());
    DBUGF("_CONTENT_LENGTH: %u", request->contentLength());
  }

  int headers = request->headers();
  int i;
  for(i=0; i<headers; i++) {
    AsyncWebHeader* h = request->getHeader(i);
    DBUGF("_HEADER[%s]: %s", h->name().c_str(), h->value().c_str());
  }

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
}

// -------------------------------------------------------------------
// Helper function to perform the standard operations on a request
// -------------------------------------------------------------------
bool requestPreProcess(AsyncWebServerRequest *request, AsyncResponseStream *&response, const __FlashStringHelper *contentType = CONTENT_TYPE_JSON)
{
  dumpRequest(request);

  if(wifi_mode_is_sta() && www_username!="" &&
     false == request->authenticate(www_username.c_str(), www_password.c_str())) {
    request->requestAuthentication(esp_hostname);
    return false;
  }

  response = request->beginResponseStream(String(contentType));
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
// Load Home page
// url: /
// -------------------------------------------------------------------
/*
void
handleHome(AsyncWebServerRequest *request) {
  if (www_username != ""
      && wifi_mode == WIFI_MODE_STA
      && !request->authenticate(www_username.c_str(),
                                www_password.c_str())) {
    return request->requestAuthentication(esp_hostname);
  }

  String page = String((wifi_mode == WIFI_MODE_AP_ONLY) ? WIFI_PAGE : HOME_PAGE);

  if (SPIFFS.exists(page)) {
    request->send(SPIFFS, page);
  } else {
    request->send(200, CONTENT_TYPE_HTML,
      F("<html><body>"
        "<h1><font color=006666>Open</font><b>EVSE</b> WiFi</h1>"
        "<p>/home.html not found, have you flashed the SPIFFS?</p>"
        "<iframe style=\"width:380px; height:50px;\" frameborder=\"0\" scrolling=\"no\"></iframe>"
        "</body></html>"));
  }
}
*/

// -------------------------------------------------------------------
// Wifi scan /scan not currently used
// url: /scan
//
// First request will return 0 results unless you start scan from somewhere else (loop/setup)
// Do not request more often than 3-5 seconds
// -------------------------------------------------------------------
void
handleScan(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
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
      json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
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
handleAPOff(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
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
handleSaveNetwork(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String qsid = request->arg("ssid");
  String qpass = request->arg("pass");
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
handleSaveEmoncms(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String apikey = request->arg("apikey");
  if(apikey.equals(DUMMY_PASSWORD)) {
    apikey = emoncms_apikey;
  }

  config_save_emoncms(isPositive(request->arg("enable")),
                      request->arg("server"),
                      request->arg("node"),
                      apikey,
                      request->arg("fingerprint"));

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
handleSaveMqtt(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String pass = request->arg("pass");
  if(pass.equals(DUMMY_PASSWORD)) {
    pass = mqtt_pass;
  }

  config_save_mqtt(isPositive(request->arg("enable")),
                   request->arg("server"),
                   request->arg("topic"),
                   request->arg("user"),
                   pass,
                   request->arg("solar"),
                   request->arg("grid_ie"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s", mqtt_server.c_str(),
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
handleDivertMode(AsyncWebServerRequest *request){
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  divertmode_update(request->arg("divertmode").toInt());

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
handleSaveAdmin(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  String quser = request->arg("user");
  String qpass = request->arg("pass");
  if(qpass.equals(DUMMY_PASSWORD)) {
    qpass = www_password;
  }

  config_save_admin(quser, qpass);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Save the Ohm keyto EEPROM
// url: /handleSaveOhmkey
// -------------------------------------------------------------------
void
handleSaveOhmkey(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  bool enabled = isPositive(request->arg("enable"));
  String qohm = request->arg("ohm");

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
handleStatus(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  if (wifi_mode_is_sta_only()) {
    s += "\"mode\":\"STA\",";
  } else if (wifi_mode_is_ap_only()) {
    s += "\"mode\":\"AP\",";
  } else if (wifi_mode_is_ap() && wifi_mode_is_sta()) {
    s += "\"mode\":\"STA+AP\",";
  }

  s += "\"wifi_client_connected\":" + String(wifi_client_connected()) + ",";
  s += "\"srssi\":" + String(WiFi.RSSI()) + ",";
  s += "\"ipaddress\":\"" + ipaddress + "\",";

  s += "\"emoncms_connected\":" + String(emoncms_connected) + ",";
  s += "\"packets_sent\":" + String(packets_sent) + ",";
  s += "\"packets_success\":" + String(packets_success) + ",";

  s += "\"mqtt_connected\":" + String(mqtt_connected()) + ",";

  s += "\"ohm_hour\":\"" + ohm_hour + "\",";

  s += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";

  s += "\"comm_sent\":" + String(comm_sent) + ",";
  s += "\"comm_success\":" + String(comm_success) + ",";

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
  s += "\"divert_update\":" + String((millis() - lastUpdate) / 1000);

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

  DBUGVAR(lastUpdate);
  DBUGVAR(millis());
  DBUGVAR((millis() - lastUpdate) / 1000);

  response->setCode(200);
  response->print(s);
  request->send(response);
}

// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfig(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String dummyPassword = String(DUMMY_PASSWORD);

  String s = "{";
  s += "\"firmware\":\"" + firmware + "\",";
  s += "\"protocol\":\"" + protocol + "\",";
  s += "\"espflash\":" + String(ESP.getFlashChipSize()) + ",";
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
  s += "\"www_username\":\"" + www_username + "\",";
  s += "\"www_password\":\"";
  if(www_password != 0) {
    s += dummyPassword;
  }
  s += "\",";
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
handleUpdate(AsyncWebServerRequest *request) {

  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  s += "\"comm_sent\":" + String(comm_sent) + ",";
  s += "\"comm_success\":" + String(comm_success) + ",";
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
handleRst(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  config_reset();
  ESP.eraseConfig();

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
handleRestart(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, CONTENT_TYPE_TEXT)) {
    return;
  }

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRestartTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Update firmware
// url: /update
// -------------------------------------------------------------------
void
handleUpdateGet(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
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

void
handleUpdatePost(AsyncWebServerRequest *request) {
  bool shouldReboot = !Update.hasError();
  AsyncWebServerResponse *response = request->beginResponse(200, CONTENT_TYPE_TEXT, shouldReboot ? "OK" : "FAIL");
  response->addHeader("Connection", "close");
  request->send(response);

  if(shouldReboot) {
    systemRestartTime = millis() + 1000;
  }
}

extern "C" uint32_t _SPIFFS_start;
extern "C" uint32_t _SPIFFS_end;
static int lastPercent = -1;

void
handleUpdateUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if(!index)
  {
    dumpRequest(request);

    DBUGF("Update Start: %s", filename.c_str());

    DBUGVAR(data[0]);
    int command = data[0] == 0xE9 ? U_FLASH : U_SPIFFS;
    size_t updateSize = U_FLASH == command ?
      (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000 :
      ((size_t) &_SPIFFS_end - (size_t) &_SPIFFS_start);

    DBUGVAR(command);
    DBUGVAR(updateSize);

    lcd_display(U_FLASH == command ? F("Updating WiFi") : F("Updating SPIFFS"), 0, 0, 0, LCD_CLEAR_LINE);
    lcd_display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE);
    lcd_loop();

    Update.runAsync(true);
    if(!Update.begin(updateSize, command)) {
#ifdef ENABLE_DEBUG
      Update.printError(DEBUG_PORT);
#endif
    }
  }
  if(!Update.hasError())
  {
    DBUGF("Update Writing %d", index);
    String text = String(index);
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
        lastPercent = percent;
      }
    }
    if(Update.write(data, len) != len) {
#ifdef ENABLE_DEBUG
      Update.printError(DEBUG_PORT);
#endif
    }
  }
  if(final)
  {
    if(Update.end(true)) {
      DBUGF("Update Success: %uB", index+len);
      lcd_display(F("Complete"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    } else {
      lcd_display(F("Error"), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
#ifdef ENABLE_DEBUG
      Update.printError(DEBUG_PORT);
#endif
    }
  }
}

String delayTimer = "0 0 0 0";

void
handleRapi(AsyncWebServerRequest *request) {
  bool json = request->hasArg("json");

  AsyncResponseStream *response;
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

  if (request->hasArg("rapi"))
  {
    String rapi = request->arg("rapi");

    // BUG: Really we should do this in the main loop not here...
    Serial.flush();
    comm_sent++;
    int ret = rapiSender.sendCmd(rapi.c_str());

    // IMPROVE: handle other errors, eg timeout
    if(0 == ret || 1 == ret)
    {
      String rapiString = rapiSender.getResponse();
      if(0 == ret) {
        comm_success++;
      }

      // Fake $GD if not supported by firmware
      if(0 == ret && rapi.startsWith(F("$ST"))) {
        delayTimer = rapi.substring(4);
      }
      if(1 == ret)
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

          comm_sent++;
          int ret = rapiSender.sendCmd(fallback.c_str());
          if(0 == ret)
          {
            String rapiString = rapiSender.getResponse();
            if(0 == ret) {
              comm_success++;
            }
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
  }
  if (false == json) {
    s += F("<script type='text/javascript'>document.getElementById('rapi').focus();</script>");
    s += F("<p></html>\r\n\r\n");
  }

  response->setCode(200);
  response->print(s);
  request->send(response);
}

void handleNotFound(AsyncWebServerRequest *request)
{
  DBUG("NOT_FOUND: ");
  dumpRequest(request);

  if(wifi_mode_is_ap_only()) {
    // Redirect to the home page in AP mode (for the captive portal)
    AsyncResponseStream *response = request->beginResponseStream(String(CONTENT_TYPE_HTML));

    String url = F("http://");
    url += ipaddress;

    String s = F("<html><body><a href=\"");
    s += url;
    s += F("\">OpenEVES</a></body></html>");

    response->setCode(301);
    response->addHeader(F("Location"), url);
    response->print(s);
    request->send(response);
  } else {
    request->send(404);
  }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if(type == WS_EVT_CONNECT) {
    DBUGF("ws[%s][%u] connect", server->url(), client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT) {
    DBUGF("ws[%s][%u] disconnect: %u", server->url(), client->id());
  } else if(type == WS_EVT_ERROR) {
    DBUGF("ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG) {
    DBUGF("ws[%s][%u] pong[%u]: %s", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len)
    {
      //the whole message is in a single frame and we got all of it's data
      DBUGF("ws[%s][%u] %s-message[%u]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", len);
    } else {
      // TODO: handle messages that are comprised of multiple frames or the frame is split into multiple packets
    }
  }
}

void
web_server_setup() {
//  SPIFFS.begin(); // mount the fs

  // Setup the static files
//  server.serveStatic("/", SPIFFS, "/")
//    .setDefaultFile("index.html");

  // Add the Web Socket server
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.addHandler(&staticFile);

  // Start server & server root html /
  //server.on("/", handleHome);

  // Handle status updates
  server.on("/status", handleStatus);
  server.on("/config", handleConfig);
#ifdef ENABLE_LEGACY_API
  server.on("/rapiupdate", handleUpdate);
#endif

  // Handle HTTP web interface button presses
  server.on("/savenetwork", handleSaveNetwork);
  server.on("/saveemoncms", handleSaveEmoncms);
  server.on("/savemqtt", handleSaveMqtt);
  server.on("/saveadmin", handleSaveAdmin);
  server.on("/saveohmkey", handleSaveOhmkey);

  server.on("/reset", handleRst);
  server.on("/restart", handleRestart);

  server.on("/rapi", handleRapi);
  server.on("/r", handleRapi);

  server.on("/scan", handleScan);
  server.on("/apoff", handleAPOff);
  server.on("/divertmode", handleDivertMode);

  // Simple Firmware Update Form
  server.on("/update", HTTP_GET, handleUpdateGet);
  server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);

  server.onNotFound(handleNotFound);
  server.begin();

  DEBUG.println("Server started");
}

void
web_server_loop() {
  Profile_Start(web_server_loop);

  // Do we need to restart the WiFi?
  if(wifiRestartTime > 0 && millis() > wifiRestartTime) {
    wifiRestartTime = 0;
    wifi_restart();
}

  // Do we need to restart MQTT?
  if(mqttRestartTime > 0 && millis() > mqttRestartTime) {
    mqttRestartTime = 0;
    mqtt_restart();
  }

  // Do we need to turn off the access point?
  if(apOffTime > 0 && millis() > apOffTime) {
    apOffTime = 0;
    wifi_turn_off_ap();
  }

  // Do we need to restart the system?
  if(systemRestartTime > 0 && millis() > systemRestartTime) {
    systemRestartTime = 0;
    wifi_disconnect();
    ESP.restart();
  }

  // Do we need to reboot the system?
  if(systemRebootTime > 0 && millis() > systemRebootTime) {
    systemRebootTime = 0;
    wifi_disconnect();
    ESP.reset();
  }

  Profile_End(web_server_loop, 5);
}

void web_server_event(String &event)
{
  ws.textAll(event);
}
