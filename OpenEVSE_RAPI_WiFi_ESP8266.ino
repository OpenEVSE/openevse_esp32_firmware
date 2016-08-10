/*
 * Copyright (c) 2015-2016 Chris Howell
 * 
 * -------------------------------------------------------------------
 * 
 * Additional Adaptation of OpenEVSE ESP Wifi
 * by Trystan Lea, Glyn Hudson, OpenEnergyMonitor
 * All adaptation GNU General Public License as below.
 *
 * -------------------------------------------------------------------
 *
 * This file is part of Open EVSE.
 * Open EVSE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * Open EVSE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with Open EVSE; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
 
// Arduino espressif libs (tested with V2.3.0)
#include <ESP8266WiFi.h>              // Connect to Wifi
#include <WiFiClientSecure.h>         // Secure https GET request
#include <ESP8266WebServer.h>         // Config portal
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>                   // Save config settings
#include "FS.h"                       // SPIFFS file-system: store web server html, CSS etc.
#include <ArduinoOTA.h>               // local OTA update from Arduino IDE
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#include <ESP8266httpUpdate.h>        // remote OTA update from server
#include <ESP8266HTTPUpdateServer.h>  // upload update
#include <DNSServer.h>                // Required for captive portal
#include <PubSubClient.h>             // MQTT https://github.com/knolleary/pubsubclient PlatformIO lib: 89

ESP8266WebServer server(80);          //Create class for Web server
WiFiClientSecure client;              // Create class for HTTPS TCP connections get_https()
HTTPClient http;                      // Create class for HTTP TCP connections get_http
WiFiClient espClient;                 // Create client for MQTT
PubSubClient mqttclient(espClient);   // Create client for MQTT
ESP8266HTTPUpdateServer httpUpdater;  // Create class for webupdate handleWebUpdate()
DNSServer dnsServer;                  // Create class DNS server, captive portal re-direct
const byte DNS_PORT = 53;

ADC_MODE(ADC_VCC);

int commDelay = 60;

const char* fwversion = "D1.1.0";

//Default SSID and PASSWORD for AP Access Point Mode
const char* softAP_ssid = "OpenEVSE";
const char* softAP_password = "openevse";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// Web server authentication (leave blank for none)
const char* www_username = "admin";
const char* www_password = "openevse";
String st, rssi;

/* hostname for mDNS. Should work at least on windows. Try http://openevse.local */
const char *esp_hostname = "openevse";

//EEPROM Strings
String esid = "";
String epass = "";  
String apikey = "";
String node = "";

int espvcc = 0;
int espflash = 0;
int espfree = 0;

String connected_network = "";
String last_datastr = "";
String status_string = "";
String ipaddress = "";

//SERVER strings and interfers for OpenEVSE Energy Monotoring
const char* host = "data.openevse.com"; //Default to use the OpenEVSE EmonCMS Server
const char* emon_fingerprint = "‎67:1E:C4:85:3B:1F:58:DB:B8:BD:B4:3D:2E:13:32:77:0D:C0:47:5E";
//const char* host = "www.emoncms.org"; //Optional to use Open Energy EmonCMS servers
//const char* emon_fingerprint = "6B 39 04 A4 BB E0 87 B2 EB B6 FE 77 CD D5 F6 A7 22 4B 3B ED";
//const char* host = "192.168.1.123";  //Optional to use your own EmonCMS server
const int httpsPort = 443;
const char* url = "/emoncms/input/post.json?node=";


String emoncms_server = "";
String emoncms_node = "";
String emoncms_apikey = "";
String emoncms_fingerprint = "";
boolean emoncms_connected = false;


//Server strings for Ohm Connect 
const char* ohm_host = "login.ohmconnect.com";
const char* ohm_url = "/verify-ohm-hour/";
String ohm = "";
const int ohm_httpsPort = 443;
const char* ohm_fingerprint = "6B 39 04 A4 BB E0 87 B2 EB B6 FE 77 CD D5 F6 A7 22 4B 3B ED";
String ohm_hour = "NotConnected";
int evse_sleep = 0;

//MQTT Settings
String mqtt_server = "";
String mqtt_topic = "";
String mqtt_user = "";
String mqtt_pass = "";
long lastMqttReconnectAttempt = 0;

// -------------------------------------------------------------------
//OTA UPDATE SETTINGS
// -------------------------------------------------------------------
//UPDATE SERVER strings and interfers for upate server
// Array of strings Used to check firmware version
const char* u_host = "217.9.195.227";
const char* u_url = "/esp/firmware.php";

const char* firmware_update_path = "/upload";

// Get running firmware version from build tag environment variable
#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)
String currentfirmware = ESCAPEQUOTE(BUILD_TAG);

// -------------------------------------------------------------------
//OpenEVSE Values
// -------------------------------------------------------------------
//UPDATE SERVER strings and interfers for upate server
// Used for sensor values and settings

int amp = 0; //OpenEVSE Current Sensor
int volt = 0; //Not currently in used
int temp1 = 0; //Sensor DS3232 Ambient
int temp2 = 0; //Sensor MCP9808 Ambient
int temp3 = 0; //Sensor TMP007 Infared
int pilot = 0; //OpenEVSE Pilot Setting
long state = 0; //OpenEVSE State
String estate = "Unknown"; // Common name for State

//Defaults OpenEVSE Settings
byte rgb_lcd = 1;
byte serial_dbg = 0;
byte auto_service = 1;
int service = 1;
int current_l1 = 0;
int current_l2 = 0;
String current_l1min = "-";
String current_l2min = "-";
String current_l1max = "-";
String current_l2max = "-";
String current_scale = "-";
String current_offset = "-";

//Default OpenEVSE Safety Configuration
byte diode_ck = 1;
byte gfci_test = 1;
byte ground_ck = 1;
byte stuck_relay = 1;
byte vent_ck = 1;
byte temp_ck = 1;
byte auto_start = 1;

String firmware = "-";
String protocol = "-";

//Default OpenEVSE Fault Counters
String gfci_count = "-";
String nognd_count = "-";
String stuck_count = "-";

//OpenEVSE Session options
String kwh_limit = "0";
String time_limit = "0";

//OpenEVSE Usage Statistics
String wattsec = "0";
String watthour_total = "0";

// Wifi mode
// 0 - STA (Client)
// 1 - AP with STA retry
// 2 - AP only
// 3 - AP + STA
int wifi_mode = 0; 
int buttonState = 0;
int clientTimeout = 0;
int i = 0;
unsigned long Timer;
unsigned long Timer2;
unsigned long packets_sent = 0;
unsigned long packets_success = 0;
unsigned long comm_sent = 0;
unsigned long comm_success = 0;

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}
// -------------------------------------------------------------------
// Start Access Point, starts on 192.168.4.1
// Access point is used for wifi network selection
// -------------------------------------------------------------------
void startAP() {
  //Serial.print("Starting Access Point");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  //Serial.print("Scan: ");
  int n = WiFi.scanNetworks();
  //Serial.print(n);
  //Serial.println(" networks found");
  st = "";
  for (int i = 0; i < n; ++i){
    st += "\""+WiFi.SSID(i)+"\"";
    rssi += "\""+String(WiFi.RSSI(i))+"\"";
    if (i<n-1) st += ",";
    if (i<n-1) rssi += ",";
  }
  delay(100);

  WiFi.softAPConfig(apIP, apIP, netMsk);
  // Create Unique SSID e.g "emonESP_XXXXXX"
  String softAP_ssid_ID = String(softAP_ssid)+"_"+String(ESP.getChipId());;
  WiFi.softAP(softAP_ssid_ID.c_str(), softAP_password);
 
  IPAddress myIP = WiFi.softAPIP();
  char tmpStr[40];
  Serial.println("$FP 0 0 SSID...OpenEVSE.");
  delay(100);
  Serial.println("$FP 0 1 PASS...openevse.");
  delay(5000);
  Serial.println("$FP 0 0 IP_Address......");
  delay(100);
  sprintf(tmpStr,"%d.%d.%d.%d",myIP[0],myIP[1],myIP[2],myIP[3]);
  //Serial.print("Access Point IP Address: ");
  Serial.print("$FP 0 1 ");
  Serial.println(tmpStr);
  ipaddress = tmpStr;
}

// -------------------------------------------------------------------
// Start Client, attempt to connect to Wifi network
// -------------------------------------------------------------------
void startClient() {
  //Serial.print("Connecting as Wifi Client to ");
  //Serial.print(esid.c_str());
  //Serial.print(" epass:");
  //Serial.println(epass.c_str());
  WiFi.begin(esid.c_str(), epass.c_str());
  
  delay(50);
  
  int t = 0;
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED){
    
    delay(500);
    t++;
    if (t >= 20){
      //Serial.println(" ");
      //Serial.println("Trying Again...");
      delay(2000);
      WiFi.disconnect();
      WiFi.begin(esid.c_str(), epass.c_str());
      t = 0;
      attempt++;
      if (attempt >= 5){
        startAP();
        // AP mode with SSID in EEPROM, connection will retry in 5 minutes
        wifi_mode = 1;
        break;
      }
    }
  }
  
  if (wifi_mode == 0 || wifi_mode == 3){
    IPAddress myAddress = WiFi.localIP();
    char tmpStr[40];
    sprintf(tmpStr,"%d.%d.%d.%d",myAddress[0],myAddress[1],myAddress[2],myAddress[3]);
    //Serial.print("Connected, IP Address: ");
    Serial.println("$FP 0 0 Client-IP.......");
    delay(100);
    Serial.print("$FP 0 1 ");
    Serial.println(tmpStr);
    
    // Copy the connected network and ipaddress to global strings for use in status request
    connected_network = esid;
    ipaddress = tmpStr;
  }
}

#define EEPROM_ESID_SIZE                 32
#define EEPROM_EPASS_SIZE                64
#define EEPROM_EMON_API_KEY_SIZE         32
#define EEPROM_EMON_SERVER_SIZE          45
#define EEPROM_EMON_NODE_SIZE            32
#define EEPROM_MQTT_SERVER_SIZE          45
#define EEPROM_MQTT_TOPIC_SIZE           32
#define EEPROM_MQTT_USER_SIZE            32
#define EEPROM_MQTT_PASS_SIZE            64
#define EEPROM_EMON_FINGERPRINT_SIZE     60
#define EEPROM_OHM_KEY_SIZE               8
#define EEPROM_SIZE                      512

#define EEPROM_ESID_START                0
#define EEPROM_ESID_END           (EEPROM_ESID_START + EEPROM_ESID_SIZE)
#define EEPROM_EPASS_START        EEPROM_ESID_END
#define EEPROM_EPASS_END          (EEPROM_EPASS_START + EEPROM_EPASS_SIZE)
#define EEPROM_EMON_API_KEY_START EEPROM_EPASS_END
#define EEPROM_EMON_API_KEY_END   (EEPROM_EMON_API_KEY_START + EEPROM_EMON_API_KEY_SIZE)
#define EEPROM_EMON_SERVER_START  EEPROM_EMON_API_KEY_END
#define EEPROM_EMON_SERVER_END    (EEPROM_EMON_SERVER_START + EEPROM_EMON_SERVER_SIZE)
#define EEPROM_EMON_NODE_START    EEPROM_EMON_SERVER_END
#define EEPROM_EMON_NODE_END      (EEPROM_EMON_NODE_START + EEPROM_EMON_NODE_SIZE)
#define EEPROM_MQTT_SERVER_START  EEPROM_EMON_NODE_END
#define EEPROM_MQTT_SERVER_END    (EEPROM_MQTT_SERVER_START + EEPROM_MQTT_SERVER_SIZE)
#define EEPROM_MQTT_TOPIC_START   EEPROM_MQTT_SERVER_END
#define EEPROM_MQTT_TOPIC_END     (EEPROM_MQTT_TOPIC_START + EEPROM_MQTT_TOPIC_SIZE)
#define EEPROM_MQTT_USER_START    EEPROM_MQTT_TOPIC_END
#define EEPROM_MQTT_USER_END      (EEPROM_MQTT_USER_START + EEPROM_MQTT_USER_SIZE)
#define EEPROM_MQTT_PASS_START    EEPROM_MQTT_USER_END
#define EEPROM_MQTT_PASS_END      (EEPROM_MQTT_PASS_START + EEPROM_MQTT_PASS_SIZE)
#define EEPROM_EMON_FINGERPRINT_START  EEPROM_MQTT_PASS_END
#define EEPROM_EMON_FINGERPRINT_END    (EEPROM_EMON_FINGERPRINT_START + EEPROM_EMON_FINGERPRINT_SIZE)
#define EEPROM_OHM_KEY_START  EEPROM_EMON_FINGERPRINT_END
#define EEPROM_OHM_KEY_END    (EEPROM_OHM_KEY_START + EEPROM_OHM_KEY_SIZE)


void ResetEEPROM(){
  //Serial.println("Erasing EEPROM");
  for (int i = 0; i < EEPROM_SIZE; ++i) { 
    EEPROM.write(i, 0);
    //Serial.print("#"); 
  }
  EEPROM.commit();   
}


void load_EEPROM_settings(){

  EEPROM.begin(EEPROM_SIZE);
  for (int i = EEPROM_ESID_START; i < EEPROM_ESID_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) esid += (char) c;
  }

  for (int i = EEPROM_EPASS_START; i < EEPROM_EPASS_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) epass += (char) c;
  }
  for (int i = EEPROM_EMON_API_KEY_START; i < EEPROM_EMON_API_KEY_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) emoncms_apikey += (char) c;
  }
  for (int i = EEPROM_EMON_SERVER_START; i < EEPROM_EMON_SERVER_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) emoncms_server += (char) c;
  }
  for (int i = EEPROM_EMON_NODE_START; i < EEPROM_EMON_NODE_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) emoncms_node += (char) c;
  }
  for (int i = EEPROM_MQTT_SERVER_START; i < EEPROM_MQTT_SERVER_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) mqtt_server += (char) c;
  }
  for (int i = EEPROM_MQTT_TOPIC_START; i < EEPROM_MQTT_TOPIC_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) mqtt_topic += (char) c;
  }
  for (int i = EEPROM_MQTT_USER_START; i < EEPROM_MQTT_USER_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) mqtt_user += (char) c;
  }
  for (int i = EEPROM_MQTT_PASS_START; i < EEPROM_MQTT_PASS_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) mqtt_pass += (char) c;
  }
  for (int i = EEPROM_EMON_FINGERPRINT_START; i < EEPROM_EMON_FINGERPRINT_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) emoncms_fingerprint += (char) c;
  }
  for (int i = EEPROM_OHM_KEY_START; i < EEPROM_OHM_KEY_END; ++i){
    byte c = EEPROM.read(i);
    if (c!=0 && c!=255) ohm += (char) c;
  }
}


// -------------------------------------------------------------------
// Load SPIFFS Home page
// url: /
// -------------------------------------------------------------------
void handleHome() {
  String s;
  File f = SPIFFS.open("/home.html", "r");
  if (f) {
    String s = f.readString();
    server.send(200, "text/html", s);
    f.close();
  }
}

// -------------------------------------------------------------------
// Handle turning Access point off
// url: /apoff
// -------------------------------------------------------------------
void handleAPOff() {
  server.send(200, "text/html", "Turning Access Point Off");
  Serial.println("Turning Access Point Off");
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
}

// -------------------------------------------------------------------
// Save selected network to EEPROM and attempt connection
// url: /savenetwork
// -------------------------------------------------------------------
void handleSaveNetwork() {
  String s;
  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");      
  esid = qsid;
  epass = qpass;
  
  qpass.replace("%21", "!");
//  qpass.replace("%22", '"');
  qpass.replace("%23", "#");
  qpass.replace("%24", "$");
  qpass.replace("%25", "%");
  qpass.replace("%26", "&");
  qpass.replace("%27", "'");
  qpass.replace("%28", "(");
  qpass.replace("%29", ")");
  qpass.replace("%2A", "*");
  qpass.replace("%2B", "+");
  qpass.replace("%2C", ",");
  qpass.replace("%2D", "-");
  qpass.replace("%2E", ".");
  qpass.replace("%2F", "/");
  qpass.replace("%3A", ":");
  qpass.replace("%3B", ";");
  qpass.replace("%3C", "<");
  qpass.replace("%3D", "=");
  qpass.replace("%3E", ">");
  qpass.replace("%3F", "?");
  qpass.replace("%40", "@");
  qpass.replace("%5B", "[");
  qpass.replace("%5C", "'\'");
  qpass.replace("%5D", "]");
  qpass.replace("%5E", "^");
  qpass.replace("%5F", "_");
  qpass.replace("%60", "`");
  qpass.replace("%7B", "{");
  qpass.replace("%7C", "|");
  qpass.replace("%7D", "}");
  qpass.replace("%7E", "~");
  qpass.replace('+', ' ');

  qsid.replace("%21", "!");
//  qsid.replace("%22", '"');
  qsid.replace("%23", "#");
  qsid.replace("%24", "$");
  qsid.replace("%25", "%");
  qsid.replace("%26", "&");
  qsid.replace("%27", "'");
  qsid.replace("%28", "(");
  qsid.replace("%29", ")");
  qsid.replace("%2A", "*");
  qsid.replace("%2B", "+");
  qsid.replace("%2C", ",");
  qsid.replace("%2D", "-");
  qsid.replace("%2E", ".");
  qsid.replace("%2F", "/");
  qsid.replace("%3A", ":");
  qsid.replace("%3B", ";");
  qsid.replace("%3C", "<");
  qsid.replace("%3D", "=");
  qsid.replace("%3E", ">");
  qsid.replace("%3F", "?");
  qsid.replace("%40", "@");
  qsid.replace("%5B", "[");
  qsid.replace("%5C", "'\'");
  qsid.replace("%5D", "]");
  qsid.replace("%5E", "^");
  qsid.replace("%5F", "_");
  qsid.replace("%60", "`");
  qsid.replace("%7B", "{");
  qsid.replace("%7C", "|");
  qsid.replace("%7D", "}");
  qsid.replace("%7E", "~");
  qsid.replace('+', ' ');
  
if (qsid != 0){
    for (int i = 0; i < EEPROM_ESID_SIZE; i++){
      if (i<qsid.length()) {
        EEPROM.write(i+EEPROM_ESID_START, qsid[i]);
      } else {
        EEPROM.write(i+EEPROM_ESID_START, 0);
      }
    }

    for (int i = 0; i < EEPROM_EPASS_SIZE; i++){
      if (i<qpass.length()) {
        EEPROM.write(i+EEPROM_EPASS_START, qpass[i]);
      } else {
        EEPROM.write(i+EEPROM_EPASS_START, 0);
      }
    }

    EEPROM.commit();
    server.send(200, "text/html", "saved");
    delay(2000);

    
    // Startup in STA + AP mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apIP, netMsk);

  // Create Unique SSID e.g "OpenEVSE_XXXXXX"
    String softAP_ssid_ID = String(softAP_ssid)+"_"+String(ESP.getChipId());;
    WiFi.softAP(softAP_ssid_ID.c_str(), softAP_password);

    /* Setup the DNS server redirecting all the domains to the apIP */
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);
    wifi_mode = 3;
    startClient();
  }
}

// -------------------------------------------------------------------
// Save Emoncms
// url: /saveemoncms
// -------------------------------------------------------------------
void handleSaveEmoncms() {
  emoncms_server = server.arg("server");
  emoncms_node = server.arg("node");
  emoncms_apikey = server.arg("apikey");
  emoncms_fingerprint = server.arg("fingerprint");
  if (emoncms_apikey!=0 && emoncms_server!=0 && emoncms_node!=0) {
    // save apikey to EEPROM
    for (int i = 0; i < EEPROM_EMON_API_KEY_SIZE; i++){
      if (i<emoncms_apikey.length()) {
        EEPROM.write(i+EEPROM_EMON_API_KEY_START, emoncms_apikey[i]);
      } else {
        EEPROM.write(i+EEPROM_EMON_API_KEY_START, 0);
      }
    }
    // save emoncms server to EEPROM max 45 characters
    for (int i = 0; i < EEPROM_EMON_SERVER_SIZE; i++){
      if (i<emoncms_server.length()) {
        EEPROM.write(i+EEPROM_EMON_SERVER_START, emoncms_server[i]);
      } else {
        EEPROM.write(i+EEPROM_EMON_SERVER_START, 0);
      }
    }
    // save emoncms node to EEPROM max 32 characters
    for (int i = 0; i < EEPROM_EMON_NODE_SIZE; i++){
      if (i<emoncms_node.length()) {
        EEPROM.write(i+EEPROM_EMON_NODE_START, emoncms_node[i]);
      } else {
        EEPROM.write(i+EEPROM_EMON_NODE_START, 0);
      }
    }
    // save emoncms HTTPS fingerprint to EEPROM max 60 characters
    if (emoncms_fingerprint!=0){
      for (int i = 0; i < EEPROM_EMON_FINGERPRINT_SIZE; i++){
        if (i<emoncms_fingerprint.length()) {
          EEPROM.write(i+EEPROM_EMON_FINGERPRINT_START, emoncms_fingerprint[i]);
        } else {
          EEPROM.write(i+EEPROM_EMON_FINGERPRINT_START, 0);
        }
      }
    }
    EEPROM.commit();
    char tmpStr[169];
    sprintf(tmpStr,"Saved: %s %s %s %s",emoncms_server.c_str(),emoncms_node.c_str(),emoncms_apikey.c_str(),emoncms_fingerprint.c_str());
    Serial.println(tmpStr);
    server.send(200, "text/html", tmpStr);
  }
}

// -------------------------------------------------------------------
// Save MQTT Config
// url: /savemqtt
// -------------------------------------------------------------------
void handleSaveMqtt() {
  mqtt_server = server.arg("server");
  mqtt_topic = server.arg("topic");
  mqtt_user = server.arg("user");
  mqtt_pass = server.arg("pass");
  if (mqtt_server!=0 && mqtt_topic!=0) {
    // Save MQTT server max 45 characters
    for (int i = 0; i < EEPROM_MQTT_SERVER_SIZE; i++){
      if (i<mqtt_server.length()) {
        EEPROM.write(i+EEPROM_MQTT_SERVER_START, mqtt_server[i]);
      } else {
        EEPROM.write(i+EEPROM_MQTT_SERVER_START, 0);
      }
    }
    // Save MQTT topic max 32 characters
    for (int i = 0; i < EEPROM_MQTT_TOPIC_SIZE; i++){
      if (i<mqtt_topic.length()) {
        EEPROM.write(i+EEPROM_MQTT_TOPIC_START, mqtt_topic[i]);
      } else {
        EEPROM.write(i+EEPROM_MQTT_TOPIC_START, 0);
      }
    }
    // Save MQTT username max 32 characters

    if (mqtt_user!=0 && mqtt_pass!=0){
      for (int i = 0; i < EEPROM_MQTT_USER_SIZE; i++){
        if (i<mqtt_user.length()) {
          EEPROM.write(i+EEPROM_MQTT_USER_START, mqtt_user[i]);
        } else {
          EEPROM.write(i+EEPROM_MQTT_USER_START, 0);
        }
      }
      // Save MQTT pass max 64 characters
      for (int i = 0; i < EEPROM_MQTT_PASS_SIZE; i++){
        if (i<mqtt_pass.length()) {
          EEPROM.write(i+EEPROM_MQTT_PASS_START, mqtt_pass[i]);
        } else {
          EEPROM.write(i+EEPROM_MQTT_PASS_START, 0);
        }
      }
    }
    EEPROM.commit();
    char tmpStr[80];
    sprintf(tmpStr,"Saved: %s %s %s %s",mqtt_server.c_str(),mqtt_topic.c_str(),mqtt_user.c_str(),mqtt_pass.c_str());
    Serial.println(tmpStr);
    server.send(200, "text/html", tmpStr);
    // If connected disconnect MQTT to trigger re-connect with new details
    if (mqttclient.connected()) {
      mqttclient.disconnect();
    }
  }
}
// -------------------------------------------------------------------
// Save Ohm COnnect Config
// url: /ohm
// -------------------------------------------------------------------

void handleSaveOhmkey() {
  ohm = server.arg("ohm");
  // save emoncms HTTPS fingerprint to EEPROM max 60 characters
    if (ohm!=0){
      for (int i = 0; i < EEPROM_OHM_KEY_SIZE; i++){
        if (i<ohm.length()) {
          EEPROM.write(i+EEPROM_OHM_KEY_START, ohm[i]);
        } else {
          EEPROM.write(i+EEPROM_OHM_KEY_START, 0);
        }
      }
   EEPROM.commit();
   server.send(200, "text/html", "Saved");
  }
}

// -------------------------------------------------------------------
// Wifi scan /scan not currently used
// url: /scan
// -------------------------------------------------------------------
void handleScan() {
  //Serial.println("WIFI Scan");
  int n = WiFi.scanNetworks();
  //Serial.print(n);
  //Serial.println(" networks found");
  st = "";
  rssi = "";
  for (int i = 0; i < n; ++i){
    st += "\""+WiFi.SSID(i)+"\"";
    rssi += "\""+String(WiFi.RSSI(i))+"\"";
    if (i<n-1) st += ",";
    if (i<n-1) rssi += ",";
  }
  server.send(200, "text/plain","[" +st+ "],[" +rssi+"]");
}


// -------------------------------------------------------------------
// url: /status
// returns wifi status
// -------------------------------------------------------------------
void handleStatus() {

  
  String s = "{";
  if (wifi_mode==0) {
    s += "\"mode\":\"STA\",";
  } else if (wifi_mode==1 || wifi_mode==2) {
    s += "\"mode\":\"AP\",";
  } else if (wifi_mode==3) {
    s += "\"mode\":\"STA+AP\",";
  }
  s += "\"networks\":["+st+"],";
  s += "\"rssi\":["+rssi+"],";

  s += "\"ssid\":\""+esid+"\",";
  s += "\"pass\":\""+epass+"\",";
  s += "\"srssi\":\""+String(WiFi.RSSI())+"\",";
  s += "\"ipaddress\":\""+ipaddress+"\",";
  s += "\"emoncms_server\":\""+emoncms_server+"\",";
  s += "\"emoncms_node\":\""+emoncms_node+"\",";
  s += "\"emoncms_apikey\":\""+emoncms_apikey+"\",";
  s += "\"emoncms_fingerprint\":\""+emoncms_fingerprint+"\",";
  s += "\"emoncms_connected\":\""+String(emoncms_connected)+"\",";
  s += "\"ohmkey\":\""+ohm+"\",";
  s += "\"packets_sent\":\""+String(packets_sent)+"\",";
  s += "\"packets_success\":\""+String(packets_success)+"\",";

  s += "\"mqtt_server\":\""+mqtt_server+"\",";
  s += "\"mqtt_topic\":\""+mqtt_topic+"\",";
  s += "\"mqtt_user\":\""+mqtt_user+"\",";
  s += "\"mqtt_pass\":\""+mqtt_pass+"\",";
  s += "\"mqtt_connected\":\""+String(mqttclient.connected())+"\",";

  s += "\"version\":\""+String(fwversion)+"\",";
  s += "\"espflash\":\""+String(espflash)+"\",";
  s += "\"free_heap\":\""+String(ESP.getFreeHeap())+"\"";

  s += "}";
  server.send(200, "text/html", s);
}

void handleConfig() {

  String s = "{";
  s += "\"firmware\":\""+firmware+"\",";
  s += "\"protocol\":\""+protocol+"\",";
  s += "\"diodet\":\""+String(diode_ck)+"\",";
  s += "\"gfcit\":\""+String(gfci_test)+"\",";
  s += "\"groundt\":\""+String(ground_ck)+"\",";
  s += "\"relayt\":\""+String(stuck_relay)+"\",";
  s += "\"ventt\":\""+String(vent_ck)+"\",";
  s += "\"tempt\":\""+String(temp_ck)+"\",";
  s += "\"service\":\""+String(service)+"\",";
  s += "\"l1min\":\""+current_l1min+"\",";
  s += "\"l1max\":\""+current_l1max+"\",";
  s += "\"l2min\":\""+current_l2min+"\",";
  s += "\"l2max\":\""+current_l2max+"\",";
  s += "\"scale\":\""+current_scale+"\",";
  s += "\"offset\":\""+current_offset+"\",";
  s += "\"gfcicount\":\""+gfci_count+"\",";
  s += "\"nogndcount\":\""+nognd_count+"\",";
  s += "\"stuckcount\":\""+stuck_count+"\",";
  s += "\"kwhlimit\":\""+kwh_limit+"\",";
  s += "\"timelimit\":\""+time_limit+"\"";
  s += "}";
  s.replace(" ", "");
  server.send(200, "text/html", s);
 }
  
 void handleUpdate() {
    
  String s = "{";
  s += "\"ohmhour\":\""+ohm_hour+"\",";
  s += "\"espvcc\":\""+String(espvcc)+"\",";
  s += "\"espfree\":\""+String(espfree)+"\",";
  s += "\"comm_sent\":\""+String(comm_sent)+"\",";
  s += "\"comm_success\":\""+String(comm_success)+"\",";
  s += "\"packets_sent\":\""+String(packets_sent)+"\",";
  s += "\"packets_success\":\""+String(packets_success)+"\",";
  s += "\"amp\":\""+String(amp)+"\",";
  s += "\"pilot\":\""+String(pilot)+"\",";
  s += "\"temp1\":\""+String(temp1)+"\",";
  s += "\"temp2\":\""+String(temp2)+"\",";
  s += "\"temp3\":\""+String(temp3)+"\",";
  s += "\"estate\":\""+String(estate)+"\",";
  s += "\"wattsec\":\""+wattsec+"\",";
  s += "\"watthour\":\""+watthour_total+"\"";
  s += "}";
  s.replace(" ", "");
 server.send(200, "text/html", s); 
}

// -------------------------------------------------------------------
// Reset config and reboot
// url: /reset
// -------------------------------------------------------------------
void handleRst() {
  ResetEEPROM();
  server.send(200, "text/html", "1");
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
}

// -------------------------------------------------------------------
// Restart (Reboot)
// url: /restart
// -------------------------------------------------------------------
void handleRestart() {
  server.send(200, "text/html", "1");
  delay(1000);
  WiFi.disconnect();
  ESP.restart();
}

// -------------------------------------------------------------------
// Check for updates and display current version
// url: /firmware
// -------------------------------------------------------------------
String handleUpdateCheck() {
  Serial.println("Running: " + currentfirmware);
  // Get latest firmware version number
  String url = u_url;
  String latestfirmware = get_http(u_host, url);
  Serial.println("Latest: " + latestfirmware);
  // Update web interface with firmware version(s)
  String s = "{";
  s += "\"current\":\""+currentfirmware+"\",";
  s += "\"latest\":\""+latestfirmware+"\"";
  s += "}";
  server.send(200, "text/html", s);
  return (latestfirmware);
}

// -------------------------------------------------------------------
// Update firmware
// url: /fupdate
// -------------------------------------------------------------------
void handleFUpdate() {
  SPIFFS.end(); // unmount filesystem
  Serial.println("UPDATING...");
  delay(500);
  t_httpUpdate_return ret = ESPhttpUpdate.update("http://" + String(u_host) + String(u_url) + "?tag=" + currentfirmware);
  String str="error";
  switch(ret) {
      case HTTP_UPDATE_FAILED:
          str = printf("Update failed error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
          break;
      case HTTP_UPDATE_NO_UPDATES:
          str="No update, running latest firmware";
          break;
      case HTTP_UPDATE_OK:
          str="Update done!";
          break;
  }
  server.send(400,"text/html",str);
  Serial.println(str);
  SPIFFS.begin(); //mount-file system
}

// -------------------------------------------------------------------
// HTTPS SECURE GET Request
// url: N/A
// -------------------------------------------------------------------

String get_https(const char* fingerprint, const char* host, String url, int httpsPort){
  // Use WiFiClient class to create TCP connections
  if (!client.connect(host, httpsPort)) {
    Serial.print(host + httpsPort); //debug
    return("Connection erromqtt_publishr");
  }
  if (client.verify(fingerprint, host)) {
    client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
     // Handle wait for reply and timeout
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        client.stop();
        return("Client Timeout");
      }
    }
    // Handle message receive
    while(client.available()){
      String line = client.readStringUntil('\r');
      Serial.println(line); //debug
      if (line.startsWith("HTTP/1.1 200 OK")) {
        return("ok");
      }
    }
  }
  else {
    return("HTTPS fingerprint no match");
  }
  return("error" + String(host));
} // end https_get

// -------------------------------------------------------------------
// HTTP GET Request
// url: N/A
// -------------------------------------------------------------------
String get_http(const char* host, String url){
  http.begin(String("http://") + host + String(url));
  int httpCode = http.GET();
  if((httpCode > 0) && (httpCode == HTTP_CODE_OK)){
    String payload = http.getString();
    Serial.println(payload);
    http.end();
    return(payload);
  }
  else{
    http.end();
    return("server error: "+String(httpCode));
  }
} // end http_get

// -------------------------------------------------------------------
// MQTT Connect
// -------------------------------------------------------------------
boolean mqtt_connect() {
  mqttclient.setServer(mqtt_server.c_str(), 1883);
  Serial.println("MQTT Connecting...");
  String strID = String(ESP.getChipId());
  if (mqttclient.connect(strID.c_str(), mqtt_user.c_str(), mqtt_pass.c_str())) {  // Attempt to connect
    Serial.println("MQTT connected");
    mqttclient.publish(mqtt_topic.c_str(), "connected"); // Once connected, publish an announcement..
  } else {
    Serial.print("MQTT failed: ");
    Serial.println(mqttclient.state());
    return(0);
  }
  return (1);
}

// -------------------------------------------------------------------
// Publish to MQTT
// Split up data string into sub topics: e.g
// data = CT1:3935,CT2:325,T1:12.5,T2:16.9,T3:11.2,T4:34.7
// base topic = emon/emonesp
// MQTT Publish: emon/emonesp/CT1 > 3935 etc..
// -------------------------------------------------------------------
void mqtt_publish(String base_topic, String data){
  String mqtt_data = "";
  String topic = base_topic + "/";
  int i=0;
  while (int(data[i])!=0){
      // Construct MQTT topic e.g. <base_topic>/CT1 e.g. emonesp/CT1
      while (data[i]!=':'){
        topic+= data[i];
        i++;
        if (int(data[i])==0){
          break;
        }
      }
      i++;
      // Construct data string to publish to above topic
      while (data[i]!=','){
        mqtt_data+= data[i];
        i++;
        if (int(data[i])==0){
          break;
        }
      }
      // send data via mqtt
      delay(100);
      mqttclient.publish(topic.c_str(), mqtt_data.c_str());
      topic= base_topic + "/";
      mqtt_data="";
      i++;
      if (int(data[i])==0) break;
  }
}


void handleRapi() {
  String s;
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Send RAPI Command<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
        s += "<p>";
        s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
        s += "</html>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRapiR() {
  String s;
  String rapiString;
  String rapi = server.arg("rapi");
  rapi.replace("%24", "$");
  rapi.replace("+", " "); 
  Serial.flush();
  Serial.println(rapi);
  delay(commDelay);
       while(Serial.available()) {
         rapiString = Serial.readStringUntil('\r');
       }    
   s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
   s += "<p>";
   s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
   s += rapi;
   s += "<p>>";
   s += rapiString;
   s += "<p></html>\r\n\r\n";
   server.send(200, "text/html", s);
}


void handleRapiRead() {
  Serial.flush(); 
  Serial.println("$GV*C1");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        firmware = rapiString.substring(firstRapiCmd, secondRapiCmd);
        protocol = rapiString.substring(secondRapiCmd);
      }
    }
  Serial.println("$GF*B1");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        int thirdRapiCmd = rapiString.indexOf(' ', secondRapiCmd+1);
        gfci_count = rapiString.substring(firstRapiCmd, secondRapiCmd);
        nognd_count = rapiString.substring(secondRapiCmd, thirdRapiCmd);
        stuck_count = rapiString.substring(thirdRapiCmd);
      }
    }
  Serial.println("$GC*AE");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        if (service == 1) {
          current_l1min = rapiString.substring(firstRapiCmd, secondRapiCmd);
          current_l1max = rapiString.substring(secondRapiCmd);
        }
        else {
          current_l2min = rapiString.substring(firstRapiCmd, secondRapiCmd);
          current_l2max = rapiString.substring(secondRapiCmd);
        }   
      }
    }
  Serial.println("$GA*AC");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        current_scale = rapiString.substring(firstRapiCmd, secondRapiCmd);
        current_offset = rapiString.substring(secondRapiCmd);
      }
    }
  Serial.println("$GH");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        kwh_limit = rapiString.substring(firstRapiCmd);
      }
    }
  Serial.println("$G3");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        time_limit = rapiString.substring(firstRapiCmd);
      }
    }
  Serial.println("$GU*C0");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        wattsec = rapiString.substring(firstRapiCmd, secondRapiCmd);
        watthour_total = rapiString.substring(secondRapiCmd);
      }
    }
    Serial.println("$GE*B0");
     comm_sent++;
     delay(commDelay);
       while(Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           comm_success++;
           String qrapi; 
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
           last_datastr = "Pilot:";
           last_datastr += pilot;
           String flag = rapiString.substring(rapiString.lastIndexOf(' '));
           long flags = strtol(flag.c_str(), NULL, 16);
           service = bitRead(flags, 0) + 1;
           diode_ck = bitRead(flags, 1);
           vent_ck = bitRead(flags, 2);
           ground_ck = bitRead(flags, 3);
           stuck_relay = bitRead(flags, 4);
           auto_service = bitRead(flags, 5);
           auto_start = bitRead(flags, 6);
           serial_dbg = bitRead(flags, 7);
           rgb_lcd = bitRead(flags, 8);
           gfci_test = bitRead(flags, 9);
           temp_ck = bitRead(flags, 10);
         }
       }  
}

// -------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------

void setup() {
	delay(2000);
	Serial.begin(115200);
  EEPROM.begin(512);
  SPIFFS.begin(); // mount the fs
  pinMode(0, INPUT);
  espflash = ESP.getFlashChipSize();
  espvcc = ESP.getVcc();
  espfree = ESP.getFreeHeap();
  //char tmpStr[40];

// Read saved settings from EEPROM
  load_EEPROM_settings();
 
  
     
WiFi.disconnect();
  // 1) If no network configured start up access point
  if (esid == 0 || esid == "")
  {
    startAP();
    wifi_mode = 2; // AP mode with no SSID in EEPROM
  }
  // 2) else try and connect to the configured network
  else
  {
    WiFi.mode(WIFI_STA);
    wifi_mode = 0;
    startClient();
  }
  
 ArduinoOTA.begin();

   // Start hostname broadcast in STA mode
  if ((wifi_mode==0 || wifi_mode==3)){
    if (MDNS.begin(esp_hostname)) {
            MDNS.addService("http", "tcp", 80);
          }
  }

  // Setup firmware upload
  httpUpdater.setup(&server, firmware_update_path);
  
  // Start server & server root html /
  server.on("/", [](){
    if(www_username!="" && !server.authenticate(www_username, www_password) && wifi_mode == 0)
      return server.requestAuthentication();
    handleHome();
  });
  
  server.on("/r", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleRapiR();
  });
  server.on("/status", [](){
  if(www_username!="" && !server.authenticate(www_username, www_password) && wifi_mode == 0)
    return server.requestAuthentication();
  handleStatus();
  });
  server.on("/reset", [](){
  if(www_username!="" && !server.authenticate(www_username, www_password) && wifi_mode == 0)
    return server.requestAuthentication();
  handleRst();
  });
  server.on("/restart", [](){
  if(www_username!="" && !server.authenticate(www_username, www_password) && wifi_mode == 0)
    return server.requestAuthentication();
  handleRestart();
  });

  server.on("/config", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleConfig();
  });
  server.on("/rapiupdate", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleUpdate();
  });
  server.on("/rapi", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleRapi();
  });
  server.on("/savenetwork", handleSaveNetwork);
  server.on("/saveemoncms", handleSaveEmoncms);
  server.on("/savemqtt", handleSaveMqtt);
  server.on("/saveohmkey", handleSaveOhmkey);
  server.on("/scan", handleScan);
  server.on("/apoff",handleAPOff);
  server.on("/firmware",handleUpdateCheck);
  server.on("/fupdate",handleFUpdate);
  

  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  
  httpUpdater.setup(&server);
	server.begin();
	//Serial.println("HTTP server started");
  Timer = millis();
  lastMqttReconnectAttempt = 0;
  delay(5000); //gives OpenEVSE time to finish self test on cold start
  handleRapiRead();
} // end setup

// -------------------------------------------------------------------
// LOOP
// -------------------------------------------------------------------

void loop() {
ArduinoOTA.handle();
server.handleClient();
dnsServer.processNextRequest();
  
 // If Wifi is connected & MQTT server has been set then connect to mqtt server
  if ((wifi_mode==0 || wifi_mode==3) && mqtt_server != 0){
    if (!mqttclient.connected()) {
      long now = millis();
      // try and reconnect continuously for first 5s then try again once every 10s
      if ( (now < 50000) || ((now - lastMqttReconnectAttempt)  > 100000) ) {
        lastMqttReconnectAttempt = now;
        if (mqtt_connect()) { // Attempt to reconnect
          lastMqttReconnectAttempt = 0;
        }
      }
    } else {
      // if MQTT connected
      mqttclient.loop();
      }
  }
  
// Remain in AP mode for 5 Minutes before resetting
if (wifi_mode == 1){
   if ((millis() - Timer) >= 300000){
     ESP.reset();
   }
}

// If Ohm Key is set - Check for Ohm Hour once every minute   
if (wifi_mode == 0 || wifi_mode == 3 && ohm != 0){
  if ((millis() - Timer2) >= 60000){
     Timer2 = millis();
     WiFiClientSecure client;
     if (!client.connect(ohm_host, ohm_httpsPort)) {
       Serial.println("connection failed");
       return;
     }
     if (client.verify(ohm_fingerprint, ohm_host)) {
       client.print(String("GET ") + ohm_url + ohm + " HTTP/1.1\r\n" +
               "Host: " + ohm_host + "\r\n" +
               "User-Agent: OpenEVSE\r\n" +
               "Connection: close\r\n\r\n");
     String line = client.readString();
     if(line.indexOf("False") > 0) {
       //Serial.println("It is not an Ohm Hour");
       ohm_hour = "False";
       if (evse_sleep == 1){
         evse_sleep = 0;
         Serial.println("$FE*AF");
       }
     }
     if(line.indexOf("True") > 0) {
       //Serial.println("Ohm Hour");
       ohm_hour = "True";
       if (evse_sleep == 0){
         evse_sleep = 1;
         Serial.println("$FS*BD");
       }
     }
    //Serial.println(line);
  } 
  else {
    Serial.println("Certificate Invalid");
    }
  }
} 

// If EMONCMS Key is set - Send data twice every minute

if (wifi_mode == 0 || wifi_mode == 3 && apikey != 0){
   if ((millis() - Timer) >= 30000){
     Timer = millis();
     espvcc = ESP.getVcc();
     espfree = ESP.getFreeHeap();
     Serial.flush();
     Serial.println("$GE*B0");
     comm_sent++;
     delay(commDelay);
       while(Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           comm_success++;
           String qrapi; 
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
         }
       }
     Serial.flush();
     Serial.println("$GS*BE");
     comm_sent++;
     delay(commDelay);
       while(Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           comm_success++; 
           String qrapi = rapiString.substring(rapiString.indexOf(' '));
           state = strtol(qrapi.c_str(), NULL, 16);
           if (state == 1) {
             estate = "Not_Connected";
           }
           if (state == 2) {
             estate = "EV_Connected";
           }
           if (state == 3) {
             estate = "Charging";
           }
           if (state == 4) {
             estate = "Vent_Required";
           }
           if (state == 5) {
             estate = "Diode_Check_Failed";
           } 
           if (state == 6) {
             estate = "GFCI_Fault";
           }
           if (state == 7) {
             estate = "No_Earth_Ground";
           }
           if (state == 8) {
             estate = "Stuck_Relay";
           }
           if (state == 9) {
             estate = "GFCI_Self_Test_Failed";
           }
           if (state == 10) {
             estate = "Over_Temperature";
           }
           if (state == 254) {
             estate = "Sleeping";
           }
           if (state == 255) {
             estate = "Disabled";
           }                 
           //last_datastr = ",State:";
           //last_datastr += estate;
         }
       }    
  
     delay(commDelay);
     Serial.flush();
     Serial.println("$GG*B2");
     comm_sent++;
     delay(commDelay);
     while(Serial.available()) {
       String rapiString = Serial.readStringUntil('\r');
       if ( rapiString.startsWith("$OK") ) {
         comm_success++;
         String qrapi; 
         qrapi = rapiString.substring(rapiString.indexOf(' '));
         amp = qrapi.toInt();
         String qrapi1;
         qrapi1 = rapiString.substring(rapiString.lastIndexOf(' '));
         volt = qrapi1.toInt();
       }
    }  
    Serial.flush(); 
    Serial.println("$GP*BB");
    comm_sent++;
    delay(commDelay);
    while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        String qrapi; 
        qrapi = rapiString.substring(rapiString.indexOf(' '));
        temp1 = qrapi.toInt();
        String qrapi1;
        int firstRapiCmd = rapiString.indexOf(' ');
        qrapi1 = rapiString.substring(rapiString.indexOf(' ', firstRapiCmd + 1 ));
        temp2 = qrapi1.toInt();
        String qrapi2;
        qrapi2 = rapiString.substring(rapiString.lastIndexOf(' '));
        temp3 = qrapi2.toInt();
      }
    } 
// Create the JSON String
  
  String s = url;
  s += String(node)+"&json={";
  String data = "";
  data += "OpenEVSE_AMP:"+String(amp)+",";
  data += "OpenEVSE_TEMP1:"+String(temp1)+",";
  data += "OpenEVSE_TEMP2:"+String(temp2)+",";
  data += "OpenEVSE_TEMP3:"+String(temp3)+",";
  data += "OpenEVSE_PILOT:"+String(pilot)+",";
  data += "OpenEVSE_STATE:"+String(state);
  s += data;
  s += "}&devicekey="+String(apikey);
     
// Send the JSON request to the server


    
    packets_sent++;
     String result="";
     if (emoncms_fingerprint!=0){
        // HTTPS on port 443 if HTTPS fingerprint is present
        Serial.println("HTTPS Enabled");
        result = get_https(emoncms_fingerprint.c_str(), emoncms_server.c_str(), url, 443);
      } else {
        // Plain HTTP if other emoncms server e.g EmonPi
        result = get_http(emoncms_server.c_str(), url);
      }
      if (result == "ok"){
        packets_success++;
        emoncms_connected = true;
      }
      else{
        emoncms_connected=false;
        Serial.print("Emoncms error: ");
        Serial.println(result);
      }
    client.print(String("GET ") + s + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
    delay(10);
    String line = client.readString();
      if (line.indexOf("ok") >= 0){
          packets_success++;
        }
    
    //Serial.println(host);
    //Serial.println(url);

  if (mqtt_server != 0){
        Serial.print("MQTT publish base-topic: "); Serial.println(mqtt_topic);
        mqtt_publish(mqtt_topic, data);
        String ram_topic = mqtt_topic + "/freeram";
        String free_ram = String(ESP.getFreeHeap());
        mqttclient.publish(ram_topic.c_str(), free_ram.c_str());
        ram_topic = "";
        free_ram ="";
      }
    
  }
}

}
