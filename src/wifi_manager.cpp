#include "emonesp.h"
#include "wifi_manager.h"
#include "config.h"
#include "lcd.h"

#ifdef ESP32
#include <WiFi.h>
#include <ESPmDNS.h>              // Resolve URL for update server etc.
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#else
#error Platform not supported
#endif

#include <DNSServer.h>                // Required for captive portal

#ifdef ENABLE_WIRED_ETHERNET
#include <ETH.h>
#endif

DNSServer dnsServer;                  // Create class DNS server, captive portal re-direct
static bool dnsServerStarted = false;
const byte DNS_PORT = 53;

// Access Point SSID, password & IP address. SSID will be softAP_ssid + chipID to make SSID unique
const char *softAP_ssid = "OpenEVSE";
const char *softAP_password = "openevse";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
int apClients = 0;

// Wifi Network Strings
String connected_network = "";
String ipaddress = "";

int client_disconnects = 0;
bool client_retry = false;
unsigned long client_retry_time = 0;

#ifdef WIFI_LED
#ifndef WIFI_LED_ON_STATE
#define WIFI_LED_ON_STATE LOW
#endif

#ifndef WIFI_LED_AP_TIME
#define WIFI_LED_AP_TIME 1000
#endif

#ifndef WIFI_LED_AP_CONNECTED_TIME
#define WIFI_LED_AP_CONNECTED_TIME 100
#endif

#ifndef WIFI_LED_STA_CONNECTING_TIME
#define WIFI_LED_STA_CONNECTING_TIME 500
#endif

int wifiLedState = !WIFI_LED_ON_STATE;
unsigned long wifiLedTimeOut = millis();
#endif

#ifndef WIFI_BUTTON
#define WIFI_BUTTON 0
#endif

#ifndef WIFI_BUTTON_PRESSED_STATE
#define WIFI_BUTTON_PRESSED_STATE LOW
#endif

#ifndef WIFI_BUTTON_PRESSED_PIN_MODE
#if LOW == WIFI_BUTTON_PRESSED_STATE
#define WIFI_BUTTON_PRESSED_PIN_MODE INPUT_PULLUP
#else
#define WIFI_BUTTON_PRESSED_PIN_MODE INPUT_PULLDOWN
#endif
#endif

#ifndef WIFI_BUTTON_AP_TIMEOUT
#define WIFI_BUTTON_AP_TIMEOUT              (5 * 1000)
#endif

#ifndef WIFI_BUTTON_FACTORY_RESET_TIMEOUT
#define WIFI_BUTTON_FACTORY_RESET_TIMEOUT   (10 * 1000)
#endif

#ifndef WIFI_CLIENT_RETRY_TIMEOUT
#define WIFI_CLIENT_RETRY_TIMEOUT (5 * 60 * 1000)
#endif

#ifndef RANDOM_SEED_CHANNEL
#define RANDOM_SEED_CHANNEL 0
#endif

int wifiButtonState = HIGH;
unsigned long wifiButtonTimeOut = millis();
bool apMessage = false;

#ifdef ENABLE_WIRED_ETHERNET
static bool eth_connected = false;
#endif

#ifdef ESP32
#include "wifi_esp32.h"
#endif

// -------------------------------------------------------------------
// Start Access Point
// Access point is used for wifi network selection
// -------------------------------------------------------------------
void
startAP() {
  DBUGLN("Starting AP");

  if (wifi_mode_is_sta()) {
    WiFi.disconnect(true);
  }

  WiFi.enableAP(true);

  WiFi.softAPConfig(apIP, apIP, netMsk);

  // Create Unique SSID e.g "emonESP_XXXXXX"
  #ifdef ESP32
  String softAP_ssid_ID =
    String(softAP_ssid) + "_" + String((uint32_t)ESP.getEfuseMac());
  #else
  String softAP_ssid_ID =
    String(softAP_ssid) + "_" + String(ESP.getChipId());
  #endif

  // Pick a random channel out of 1, 6 or 11
  int channel = (random(3) * 5) + 1;
  WiFi.softAP(softAP_ssid_ID.c_str(), softAP_password, channel);

  // Setup the DNS server redirecting all the domains to the apIP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServerStarted = dnsServer.start(DNS_PORT, "*", apIP);

  IPAddress myIP = WiFi.softAPIP();
  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
  ipaddress = tmpStr;
  DEBUG.print("AP IP Address: ");
  DEBUG.println(tmpStr);
  lcd_display(F("SSID: OpenEVSE"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd_display(F("Pass: openevse"), 0, 1, 15 * 1000, LCD_CLEAR_LINE);

  apClients = 0;
}

// -------------------------------------------------------------------
// Start Client, attempt to connect to Wifi network
// -------------------------------------------------------------------
void
startClient()
{
  DEBUG.print("Connecting to SSID: ");
  DEBUG.println(esid.c_str());
  // DEBUG.print(" epass:");
  // DEBUG.println(epass.c_str());

  client_disconnects = 0;

  WiFi.begin(esid.c_str(), epass.c_str());
#ifdef ESP32
  WiFi.setHostname(esp_hostname.c_str());
#else
  WiFi.hostname(esp_hostname.c_str());
#endif // !ESP32
  WiFi.enableSTA(true);
}

static void wifi_start()
{
  // 1) If no network configured start up access point
  if (esid == 0 || esid == "")
  {
    startAP();
  }
  // 2) else try and connect to the configured network
  else
  {
    startClient();
  }
}

static void wifi_connected(IPAddress myAddress)
{
#ifdef WIFI_LED
  wifiLedState = WIFI_LED_ON_STATE;
  digitalWrite(WIFI_LED, wifiLedState);
#endif

  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myAddress[0], myAddress[1], myAddress[2], myAddress[3]);
  ipaddress = tmpStr;
  DEBUG.print("Connected, IP: ");
  DEBUG.println(tmpStr);
  lcd_display(F("IP Address"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd_display(tmpStr, 0, 1, 5000, LCD_CLEAR_LINE);
}

static void wifi_onStationModeConnected(const WiFiEventStationModeConnected &event) {
  DBUGF("Connected to %s", event.ssid.c_str());
}

static void wifi_onStationModeGotIP(const WiFiEventStationModeGotIP &event)
{
  wifi_connected(WiFi.localIP());

  // Copy the connected network and ipaddress to global strings for use in status request
  connected_network = esid;

  // Clear any error state
  client_disconnects = 0;
  client_retry = false;
}

static void wifi_onStationModeDisconnected(const WiFiEventStationModeDisconnected &event)
{
  DBUGF("WiFi dissconnected: %s",
  WIFI_DISCONNECT_REASON_UNSPECIFIED == event.reason ? "WIFI_DISCONNECT_REASON_UNSPECIFIED" :
  WIFI_DISCONNECT_REASON_AUTH_EXPIRE == event.reason ? "WIFI_DISCONNECT_REASON_AUTH_EXPIRE" :
  WIFI_DISCONNECT_REASON_AUTH_LEAVE == event.reason ? "WIFI_DISCONNECT_REASON_AUTH_LEAVE" :
  WIFI_DISCONNECT_REASON_ASSOC_EXPIRE == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_EXPIRE" :
  WIFI_DISCONNECT_REASON_ASSOC_TOOMANY == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_TOOMANY" :
  WIFI_DISCONNECT_REASON_NOT_AUTHED == event.reason ? "WIFI_DISCONNECT_REASON_NOT_AUTHED" :
  WIFI_DISCONNECT_REASON_NOT_ASSOCED == event.reason ? "WIFI_DISCONNECT_REASON_NOT_ASSOCED" :
  WIFI_DISCONNECT_REASON_ASSOC_LEAVE == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_LEAVE" :
  WIFI_DISCONNECT_REASON_ASSOC_NOT_AUTHED == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_NOT_AUTHED" :
  WIFI_DISCONNECT_REASON_DISASSOC_PWRCAP_BAD == event.reason ? "WIFI_DISCONNECT_REASON_DISASSOC_PWRCAP_BAD" :
  WIFI_DISCONNECT_REASON_DISASSOC_SUPCHAN_BAD == event.reason ? "WIFI_DISCONNECT_REASON_DISASSOC_SUPCHAN_BAD" :
  WIFI_DISCONNECT_REASON_IE_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_IE_INVALID" :
  WIFI_DISCONNECT_REASON_MIC_FAILURE == event.reason ? "WIFI_DISCONNECT_REASON_MIC_FAILURE" :
  WIFI_DISCONNECT_REASON_4WAY_HANDSHAKE_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_4WAY_HANDSHAKE_TIMEOUT" :
  WIFI_DISCONNECT_REASON_GROUP_KEY_UPDATE_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_GROUP_KEY_UPDATE_TIMEOUT" :
  WIFI_DISCONNECT_REASON_IE_IN_4WAY_DIFFERS == event.reason ? "WIFI_DISCONNECT_REASON_IE_IN_4WAY_DIFFERS" :
  WIFI_DISCONNECT_REASON_GROUP_CIPHER_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_GROUP_CIPHER_INVALID" :
  WIFI_DISCONNECT_REASON_PAIRWISE_CIPHER_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_PAIRWISE_CIPHER_INVALID" :
  WIFI_DISCONNECT_REASON_AKMP_INVALID == event.reason ? "WIFI_DISCONNECT_REASON_AKMP_INVALID" :
  WIFI_DISCONNECT_REASON_UNSUPP_RSN_IE_VERSION == event.reason ? "WIFI_DISCONNECT_REASON_UNSUPP_RSN_IE_VERSION" :
  WIFI_DISCONNECT_REASON_INVALID_RSN_IE_CAP == event.reason ? "WIFI_DISCONNECT_REASON_INVALID_RSN_IE_CAP" :
  WIFI_DISCONNECT_REASON_802_1X_AUTH_FAILED == event.reason ? "WIFI_DISCONNECT_REASON_802_1X_AUTH_FAILED" :
  WIFI_DISCONNECT_REASON_CIPHER_SUITE_REJECTED == event.reason ? "WIFI_DISCONNECT_REASON_CIPHER_SUITE_REJECTED" :
  WIFI_DISCONNECT_REASON_BEACON_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_BEACON_TIMEOUT" :
  WIFI_DISCONNECT_REASON_NO_AP_FOUND == event.reason ? "WIFI_DISCONNECT_REASON_NO_AP_FOUND" :
  WIFI_DISCONNECT_REASON_AUTH_FAIL == event.reason ? "WIFI_DISCONNECT_REASON_AUTH_FAIL" :
  WIFI_DISCONNECT_REASON_ASSOC_FAIL == event.reason ? "WIFI_DISCONNECT_REASON_ASSOC_FAIL" :
  WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT == event.reason ? "WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT" :
  "UNKNOWN");

  client_disconnects++;
}

static void wifi_onAPModeStationConnected(const WiFiEventSoftAPModeStationConnected &event) {
  lcd_display(F("IP Address"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd_display(ipaddress, 0, 1, (0 == apClients ? 15 : 5) * 1000, LCD_CLEAR_LINE);
  apClients++;
};

static void wifi_onAPModeStationDisconnected(const WiFiEventSoftAPModeStationDisconnected &event) {
  apClients--;
};

#ifdef ESP32
void wifi_event(WiFiEvent_t event, system_event_info_t info)
{
  DBUGF("Got Network event %s",
    SYSTEM_EVENT_WIFI_READY == event ? "SYSTEM_EVENT_WIFI_READY" :
    SYSTEM_EVENT_SCAN_DONE == event ? "SYSTEM_EVENT_SCAN_DONE" :
    SYSTEM_EVENT_STA_START == event ? "SYSTEM_EVENT_STA_START" :
    SYSTEM_EVENT_STA_STOP == event ? "SYSTEM_EVENT_STA_STOP" :
    SYSTEM_EVENT_STA_CONNECTED == event ? "SYSTEM_EVENT_STA_CONNECTED" :
    SYSTEM_EVENT_STA_DISCONNECTED == event ? "SYSTEM_EVENT_STA_DISCONNECTED" :
    SYSTEM_EVENT_STA_AUTHMODE_CHANGE == event ? "SYSTEM_EVENT_STA_AUTHMODE_CHANGE" :
    SYSTEM_EVENT_STA_GOT_IP == event ? "SYSTEM_EVENT_STA_GOT_IP" :
    SYSTEM_EVENT_STA_LOST_IP == event ? "SYSTEM_EVENT_STA_LOST_IP" :
    SYSTEM_EVENT_STA_WPS_ER_SUCCESS == event ? "SYSTEM_EVENT_STA_WPS_ER_SUCCESS" :
    SYSTEM_EVENT_STA_WPS_ER_FAILED == event ? "SYSTEM_EVENT_STA_WPS_ER_FAILED" :
    SYSTEM_EVENT_STA_WPS_ER_TIMEOUT == event ? "SYSTEM_EVENT_STA_WPS_ER_TIMEOUT" :
    SYSTEM_EVENT_STA_WPS_ER_PIN == event ? "SYSTEM_EVENT_STA_WPS_ER_PIN" :
    SYSTEM_EVENT_AP_START == event ? "SYSTEM_EVENT_AP_START" :
    SYSTEM_EVENT_AP_STOP == event ? "SYSTEM_EVENT_AP_STOP" :
    SYSTEM_EVENT_AP_STACONNECTED == event ? "SYSTEM_EVENT_AP_STACONNECTED" :
    SYSTEM_EVENT_AP_STADISCONNECTED == event ? "SYSTEM_EVENT_AP_STADISCONNECTED" :
    SYSTEM_EVENT_AP_STAIPASSIGNED == event ? "SYSTEM_EVENT_AP_STAIPASSIGNED" :
    SYSTEM_EVENT_AP_PROBEREQRECVED == event ? "SYSTEM_EVENT_AP_PROBEREQRECVED" :
    SYSTEM_EVENT_GOT_IP6 == event ? "SYSTEM_EVENT_GOT_IP6" :
    SYSTEM_EVENT_ETH_START == event ? "SYSTEM_EVENT_ETH_START" :
    SYSTEM_EVENT_ETH_STOP == event ? "SYSTEM_EVENT_ETH_STOP" :
    SYSTEM_EVENT_ETH_CONNECTED == event ? "SYSTEM_EVENT_ETH_CONNECTED" :
    SYSTEM_EVENT_ETH_DISCONNECTED == event ? "SYSTEM_EVENT_ETH_DISCONNECTED" :
    SYSTEM_EVENT_ETH_GOT_IP == event ? "SYSTEM_EVENT_ETH_GOT_IP" :
    "UNKNOWN"
  );

  switch (event)
  {
    case SYSTEM_EVENT_STA_CONNECTED:
    {
      auto& src = info.connected;
      WiFiEventStationModeConnected dst;
      dst.ssid = String(reinterpret_cast<char*>(src.ssid));
      memcpy(dst.bssid, src.bssid, 6);
      dst.channel = src.channel;
      wifi_onStationModeConnected(dst);
    } break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
      auto& src = info.disconnected;
      WiFiEventStationModeDisconnected dst;
      dst.ssid = String(reinterpret_cast<char*>(src.ssid));
      memcpy(dst.bssid, src.bssid, 6);
      dst.reason = static_cast<WiFiDisconnectReason>(src.reason);
      wifi_onStationModeDisconnected(dst);
    } break;
    case SYSTEM_EVENT_STA_GOT_IP:
    {
      auto& src = info.got_ip.ip_info;
      WiFiEventStationModeGotIP dst;
      dst.ip = src.ip.addr;
      dst.mask = src.netmask.addr;
      dst.gw = src.gw.addr;
      wifi_onStationModeGotIP(dst);
    } break;
    case SYSTEM_EVENT_AP_STACONNECTED:
    {
      auto& src = info.sta_connected;
      WiFiEventSoftAPModeStationConnected dst;
      memcpy(dst.mac, src.mac, 6);
      dst.aid = src.aid;
      wifi_onAPModeStationConnected(dst);
    } break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
    {
      auto& src = info.sta_disconnected;
      WiFiEventSoftAPModeStationDisconnected dst;
      memcpy(dst.mac, src.mac, 6);
      dst.aid = src.aid;
      wifi_onAPModeStationDisconnected(dst);
    } break;
#ifdef ENABLE_WIRED_ETHERNET
    case SYSTEM_EVENT_ETH_START:
      DBUGLN("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      DBUGLN("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      DBUG("ETH MAC: ");
      DBUG(ETH.macAddress());
      DBUG(", IPv4: ");
      DBUG(ETH.localIP());
      if (ETH.fullDuplex()) {
        DBUG(", FULL_DUPLEX");
      }
      DBUG(", ");
      DBUG(ETH.linkSpeed());
      DBUGLN("Mbps");
      wifi_connected(ETH.localIP());
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      DBUGLN("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      DBUGLN("ETH Stopped");
      eth_connected = false;
      break;
#endif
    default:
      break;
  }
}
#endif

void
wifi_setup() {
#ifdef WIFI_LED
  DBUGF("Configuring pin %d for LED", WIFI_LED);
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, wifiLedState);
#endif

#if defined(WIFI_BUTTON) && WIFI_BUTTON != WIFI_LED
  DBUGF("Configuring pin %d for Button", WIFI_BUTTON);
  pinMode(WIFI_BUTTON, WIFI_BUTTON_PRESSED_PIN_MODE);
#endif

  randomSeed(analogRead(RANDOM_SEED_CHANNEL));

  // If we have an SSID configured at this point we have likely
  // been running another firmware, clear the results
  if(wifi_is_client_configured()) {
    WiFi.persistent(true);
    WiFi.disconnect();
    #ifndef ESP32
    ESP.eraseConfig();
    #endif // !ESP32
  }

  // Stop the WiFi module
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

#ifdef ESP32
  WiFi.onEvent(wifi_event);
#else
  static auto _onStationModeConnected = WiFi.onStationModeConnected(wifi_onStationModeConnected);
  static auto _onStationModeGotIP = WiFi.onStationModeGotIP(wifi_onStationModeGotIP);
  static auto _onStationModeDisconnected = WiFi.onStationModeDisconnected(wifi_onStationModeDisconnected);
  static auto _onSoftAPModeStationConnected = WiFi.onSoftAPModeStationConnected(wifi_onAPModeStationConnected);
  static auto _onSoftAPModeStationDisconnected = WiFi.onSoftAPModeStationDisconnected(wifi_onAPModeStationDisconnected);
#endif

  wifi_start();

#ifdef ENABLE_WIRED_ETHERNET
  ETH.begin();
#endif

  if (MDNS.begin(esp_hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
  }
}

void
wifi_loop()
{
  Profile_Start(wifi_loop);

  //bool isClient = wifi_mode_is_sta();
  bool isClientOnly = wifi_mode_is_sta_only();
  //bool isAp = wifi_mode_is_ap();
  bool isApOnly = wifi_mode_is_ap_only();

#ifdef WIFI_LED
  if ((isApOnly || !WiFi.isConnected()) &&
      millis() > wifiLedTimeOut)
  {
    wifiLedState = !wifiLedState;
    digitalWrite(WIFI_LED, wifiLedState);

    int ledTime = isApOnly ? (0 == apClients ? WIFI_LED_AP_TIME : WIFI_LED_AP_CONNECTED_TIME) : WIFI_LED_STA_CONNECTING_TIME;
    wifiLedTimeOut = millis() + ledTime;
  }
#endif

#if defined(WIFI_LED) && WIFI_BUTTON == WIFI_LED
  digitalWrite(WIFI_BUTTON, HIGH);
  pinMode(WIFI_BUTTON, WIFI_BUTTON_PRESSED_PIN_MODE);
#endif

  // Pressing the boot button for 5 seconds will turn on AP mode, 10 seconds will factory reset
  int button = digitalRead(WIFI_BUTTON);

#if defined(WIFI_LED) && WIFI_BUTTON == WIFI_LED
  pinMode(WIFI_BUTTON, OUTPUT);
  digitalWrite(WIFI_LED, wifiLedState);
#endif

  //DBUGF("%lu %d %d", millis() - wifiButtonTimeOut, button, wifiButtonState);
  if(wifiButtonState != button)
  {
    wifiButtonState = button;
    if(WIFI_BUTTON_PRESSED_STATE == button) {
      DBUGF("Button pressed");
      wifiButtonTimeOut = millis();
      apMessage = false;
    } else {
      DBUGF("Button released");
      if(millis() > wifiButtonTimeOut + WIFI_BUTTON_AP_TIMEOUT) {
        startAP();
      }
    }
  }

  if(WIFI_BUTTON_PRESSED_STATE == wifiButtonState && millis() > wifiButtonTimeOut + WIFI_BUTTON_FACTORY_RESET_TIMEOUT)
  {
    DBUGLN("*** Factory Reset ***");

    lcd_display(F("Factory Reset"), 0, 0, 0, LCD_CLEAR_LINE);
    lcd_display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE);
    lcd_loop();

    delay(1000);

    config_reset();
#ifdef ESP8266
    ESP.eraseConfig();
#endif

    delay(50);
#ifdef ESP32
    ESP.restart();
#elif defined(ESP8266)
    ESP.reset();
#endif
  }
  else if(false == apMessage && LOW == wifiButtonState && millis() > wifiButtonTimeOut + WIFI_BUTTON_AP_TIMEOUT)
  {
    lcd_display(F("Access Point"), 0, 0, 0, LCD_CLEAR_LINE);
    lcd_display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE);
    lcd_loop();
    apMessage = true;
  }

  // Manage state while connecting
  if(isClientOnly && !WiFi.isConnected())
  {
    // If we have failed to connect turn on the AP
    if(client_disconnects > 2) {
      startAP();
      client_retry = true;
      client_retry_time = millis() + WIFI_CLIENT_RETRY_TIMEOUT;
    }
  }

  // Remain in AP mode for 5 Minutes before resetting
  if(isApOnly && 0 == apClients && client_retry && millis() > client_retry_time) {
    DEBUG.println("client re-try, resetting");
    delay(50);
    #ifdef ESP32
    esp_restart();
    #else
    ESP.reset();
    #endif
  }

  if(dnsServerStarted) {
    dnsServer.processNextRequest(); // Captive portal DNS re-dierct
  }

  Profile_End(wifi_loop, 5);
}

void
wifi_restart() {
  wifi_disconnect();
  wifi_start();
}

void
wifi_disconnect() {
  wifi_turn_off_ap();
  if (wifi_mode_is_sta()) {
    WiFi.disconnect(true);
  }
}

void wifi_turn_off_ap()
{
  if(wifi_mode_is_ap())
  {
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    dnsServerStarted = false;
  }
}

void wifi_turn_on_ap()
{
  DBUGF("wifi_turn_on_ap %d", WiFi.getMode());
  if(!wifi_mode_is_ap()) {
    startAP();
  }
}

bool wifi_client_connected()
{
  return WiFi.isConnected() && (WIFI_STA == (WiFi.getMode() & WIFI_STA));
}
