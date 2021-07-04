#include "emonesp.h"
#include "net_manager.h"
#include "app_config.h"
#include "lcd.h"
#include "espal.h"
#include "time_man.h"

#include "LedManagerTask.h"

#ifdef ESP32
#include <WiFi.h>
#include <ESPmDNS.h>              // Resolve URL for update server etc.
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#else
#error Platform not supported
#endif

#include <MongooseCore.h>

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


int wifiButtonState = !WIFI_BUTTON_PRESSED_STATE;
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

  DBUGVAR(net_wifi_mode_is_ap());
  DBUGVAR(net_wifi_mode_is_sta());

  if (net_wifi_mode_is_sta()) {
    WiFi.disconnect(true);
  }

  WiFi.enableAP(true);

  WiFi.softAPConfig(apIP, apIP, netMsk);

  // Create Unique SSID e.g "emonESP_XXXXXX"
  String softAP_ssid_ID =
    String(softAP_ssid) + "_" + ESPAL.getShortId();

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
  lcd.display(softAP_ssid_ID, 0, 0, 0, LCD_CLEAR_LINE);
  lcd.display(String(F("Pass: ")) + softAP_password, 0, 1, 15 * 1000, LCD_CLEAR_LINE);

  ledManager.setWifiMode(false, false);

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

#ifndef ESP32
  WiFi.hostname(esp_hostname.c_str());
#endif // !ESP32

  WiFi.begin(esid.c_str(), epass.c_str());

  ledManager.setWifiMode(true, false);
}

static void net_wifi_start()
{
  client_disconnects = 0;
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

static void display_state()
{
  lcd.display(F("Hostname:"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd.display(esp_hostname.c_str(), 0, 1, 5000, LCD_CLEAR_LINE);

  lcd.display(F("IP Address:"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd.display(ipaddress.c_str(), 0, 1, 5000, LCD_CLEAR_LINE);
}

static void net_connected(IPAddress myAddress)
{
  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myAddress[0], myAddress[1], myAddress[2], myAddress[3]);
  ipaddress = tmpStr;
  DEBUG.print("Connected, IP: ");
  DEBUG.println(tmpStr);

  display_state();

  Mongoose.ipConfigChanged();

  ledManager.setWifiMode(true, true);

  timeManager.setHost(sntp_hostname.c_str());
}

static void net_wifi_onStationModeConnected(const WiFiEventStationModeConnected &event) {
  DBUGF("Connected to %s", event.ssid.c_str());
}

static void net_wifi_onStationModeGotIP(const WiFiEventStationModeGotIP &event)
{
  net_connected(WiFi.localIP());

  // Copy the connected network and ipaddress to global strings for use in status request
  connected_network = esid;

  // Clear any error state
  client_disconnects = 0;
  client_retry = false;
}

static void net_wifi_onStationModeDisconnected(const WiFiEventStationModeDisconnected &event)
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

  if(net_wifi_mode_is_sta()) {
    startClient();
  }
}

static void net_wifi_onAPModeStationConnected(const WiFiEventSoftAPModeStationConnected &event) {
  lcd.display(F("IP Address"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd.display(ipaddress, 0, 1, (0 == apClients ? 15 : 5) * 1000, LCD_CLEAR_LINE);

  ledManager.setWifiMode(false, true);

  apClients++;
};

static void net_wifi_onAPModeStationDisconnected(const WiFiEventSoftAPModeStationDisconnected &event) {
  apClients--;

  if(0 == apClients) {
    ledManager.setWifiMode(false, false);
  }
};

#ifdef ESP32
void net_event(WiFiEvent_t event, system_event_info_t info)
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
    case SYSTEM_EVENT_AP_START:
    {
      if(WiFi.softAPsetHostname(esp_hostname.c_str())) {
        DBUGF("Set host name to %s", WiFi.softAPgetHostname());
      } else {
        DBUGF("Setting host name failed: %s", esp_hostname.c_str());
      }
    } break;
    case SYSTEM_EVENT_STA_START:
    {
      if(WiFi.setHostname(esp_hostname.c_str())) {
        DBUGF("Set host name to %s", WiFi.getHostname());
      } else {
        DBUGF("Setting host name failed: %s", esp_hostname.c_str());
      }
    } break;
    case SYSTEM_EVENT_STA_CONNECTED:
    {
      auto& src = info.connected;
      WiFiEventStationModeConnected dst;
      dst.ssid = String(reinterpret_cast<char*>(src.ssid));
      memcpy(dst.bssid, src.bssid, 6);
      dst.channel = src.channel;
      net_wifi_onStationModeConnected(dst);
    } break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
      auto& src = info.disconnected;
      WiFiEventStationModeDisconnected dst;
      dst.ssid = String(reinterpret_cast<char*>(src.ssid));
      memcpy(dst.bssid, src.bssid, 6);
      dst.reason = static_cast<WiFiDisconnectReason>(src.reason);
      net_wifi_onStationModeDisconnected(dst);
    } break;
    case SYSTEM_EVENT_STA_GOT_IP:
    {
      auto& src = info.got_ip.ip_info;
      WiFiEventStationModeGotIP dst;
      dst.ip = src.ip.addr;
      dst.mask = src.netmask.addr;
      dst.gw = src.gw.addr;
      net_wifi_onStationModeGotIP(dst);
    } break;
    case SYSTEM_EVENT_AP_STACONNECTED:
    {
      auto& src = info.sta_connected;
      WiFiEventSoftAPModeStationConnected dst;
      memcpy(dst.mac, src.mac, 6);
      dst.aid = src.aid;
      net_wifi_onAPModeStationConnected(dst);
    } break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
    {
      auto& src = info.sta_disconnected;
      WiFiEventSoftAPModeStationDisconnected dst;
      memcpy(dst.mac, src.mac, 6);
      dst.aid = src.aid;
      net_wifi_onAPModeStationDisconnected(dst);
    } break;
#ifdef ENABLE_WIRED_ETHERNET
    case SYSTEM_EVENT_ETH_START:
      DBUGLN("ETH Started");
      //set eth hostname here
      if(ETH.setHostname(esp_hostname.c_str())) {
        DBUGF("Set host name to %s", ETH.getHostname());
      } else {
        DBUGF("Setting host name failed: %s", esp_hostname.c_str());
      }
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
      net_connected(ETH.localIP());
      eth_connected = true;
      net_wifi_disconnect();
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      DBUGLN("ETH Disconnected");
      eth_connected = false;
      net_wifi_start();
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
net_setup()
{
  randomSeed(analogRead(RANDOM_SEED_CHANNEL));

  // If we have an SSID configured at this point we have likely
  // been running another firmware, clear the results
  if(net_wifi_is_client_configured()) {
    WiFi.persistent(true);
    WiFi.disconnect();
    ESPAL.eraseConfig();
  }

  // Stop the WiFi module
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

#ifdef ESP32
  WiFi.onEvent(net_event);
#else
  static auto _onStationModeConnected = WiFi.onStationModeConnected(net_wifi_onStationModeConnected);
  static auto _onStationModeGotIP = WiFi.onStationModeGotIP(net_wifi_onStationModeGotIP);
  static auto _onStationModeDisconnected = WiFi.onStationModeDisconnected(net_wifi_onStationModeDisconnected);
  static auto _onSoftAPModeStationConnected = WiFi.onSoftAPModeStationConnected(net_wifi_onAPModeStationConnected);
  static auto _onSoftAPModeStationDisconnected = WiFi.onSoftAPModeStationDisconnected(net_wifi_onAPModeStationDisconnected);
#endif

  net_wifi_start();

#ifdef ENABLE_WIRED_ETHERNET
  ETH.begin();
#endif

  if (MDNS.begin(esp_hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
  }
}

void
net_loop()
{
  Profile_Start(net_loop);

  //bool isClient = net_wifi_mode_is_sta();
  bool isClientOnly = net_wifi_mode_is_sta_only();
  //bool isAp = net_wifi_mode_is_ap();
  bool isApOnly = net_wifi_mode_is_ap_only();


  int button = ledManager.getButtonPressed();

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
      } else {
        display_state();
      }
    }
  }

  if(WIFI_BUTTON_PRESSED_STATE == wifiButtonState && millis() > wifiButtonTimeOut + WIFI_BUTTON_FACTORY_RESET_TIMEOUT)
  {
    DBUGLN("*** Factory Reset ***");

    lcd.display(F("Factory Reset"), 0, 0, 0, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);

    delay(1000);

    config_reset();
    ESPAL.eraseConfig();

    delay(50);
    ESPAL.reset();
  }
  else if(false == apMessage && LOW == wifiButtonState && millis() > wifiButtonTimeOut + WIFI_BUTTON_AP_TIMEOUT)
  {
    lcd.display(F("Access Point"), 0, 0, 0, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
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
    ESPAL.reset();
  }

  if(dnsServerStarted) {
    dnsServer.processNextRequest(); // Captive portal DNS re-dierct
  }

  Profile_End(net_loop, 5);
}

void
net_wifi_restart() {
  DBUGF("net_wifi_restart %d", WiFi.getMode());
  net_wifi_disconnect();
  net_wifi_start();
}

void
net_wifi_disconnect() {
  DBUGF("net_wifi_disconnect %d", WiFi.getMode());
  net_wifi_turn_off_ap();
  if (net_wifi_mode_is_sta()) {
    WiFi.disconnect(true);
  }
}

void net_wifi_turn_off_ap()
{
  DBUGF("net_wifi_turn_off_ap %d", WiFi.getMode());
  if(net_wifi_mode_is_ap())
  {
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    dnsServerStarted = false;
  }
}

void net_wifi_turn_on_ap()
{
  DBUGF("net_wifi_turn_on_ap %d", WiFi.getMode());
  if(!net_wifi_mode_is_ap()) {
    startAP();
  }
}

bool net_is_connected()
{
  return net_wifi_client_connected() || net_eth_connected();
}

bool net_wifi_client_connected()
{
  return WiFi.isConnected() && (WIFI_STA == (WiFi.getMode() & WIFI_STA));
}

bool net_eth_connected()
{
#ifdef ENABLE_WIRED_ETHERNET
  return eth_connected;
#else
  return false;
#endif
}
