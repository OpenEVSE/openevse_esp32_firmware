#include "emonesp.h"
#include "wifi.h"
#include "config.h"
#include "lcd.h"

#include <ESP8266WiFi.h>              // Connect to Wifi
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#include <DNSServer.h>                // Required for captive portal

DNSServer dnsServer;                  // Create class DNS server, captive portal re-direct
const byte DNS_PORT = 53;

// Access Point SSID, password & IP address. SSID will be softAP_ssid + chipID to make SSID unique
const char *softAP_ssid = "OpenEVSE";
const char *softAP_password = "openevse";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
int apClients = 0;

// hostname for mDNS. Should work at least on windows. Try http://openevse or http://openevse.local
const char *esp_hostname = "openevse";

// Wifi Network Strings
String connected_network = "";
String status_string = "";
String ipaddress = "";

unsigned long Timer;
String st, rssi;

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

#ifndef WIFI_BUTTON_TIMEOUT
#define WIFI_BUTTON_TIMEOUT 5 * 1000
#endif

int wifiButtonState = HIGH;
unsigned long wifiButtonTimeOut = millis();

// -------------------------------------------------------------------
int wifi_mode = WIFI_MODE_STA;

// Client connection state
enum {
  Client_Disconnected,
  Client_Connecting,
  Client_Retry,
  Client_Connected
} clientState = Client_Disconnected;

void sync_mode(bool retry = false)
{
  DBUGVAR(WiFi.getMode());
  switch(WiFi.getMode())
  {
    case WIFI_OFF:
      break;
    case WIFI_STA:
      wifi_mode = WIFI_MODE_STA;
      break;
    case WIFI_AP:
      wifi_mode = WIFI_MODE_AP_ONLY;
      break;
    case WIFI_AP_STA:
      wifi_mode = retry ? WIFI_MODE_AP_STA_RETRY : WIFI_MODE_AP_AND_STA;
      break;
  }

  DBUGVAR(wifi_mode);
}

// -------------------------------------------------------------------
// Start Access Point
// Access point is used for wifi network selection
// -------------------------------------------------------------------
void
startAP() {
  DBUGLN("Starting AP");

  if((WiFi.getMode() & WIFI_STA) && WiFi.isConnected()) {
    WiFi.disconnect(true);
    clientState = Client_Disconnected;
  }

  WiFi.enableAP(true);

  WiFi.softAPConfig(apIP, apIP, netMsk);
  // Create Unique SSID e.g "emonESP_XXXXXX"
  String softAP_ssid_ID =
    String(softAP_ssid) + "_" + String(ESP.getChipId());
  // Pick a random channel out of 1, 6 or 11
  int channel = (random(3) * 5) + 1;
  WiFi.softAP(softAP_ssid_ID.c_str(), softAP_password, channel);

  // Setup the DNS server redirecting all the domains to the apIP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  IPAddress myIP = WiFi.softAPIP();
  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
  ipaddress = tmpStr;
  DEBUG.print("AP IP Address: ");
  DEBUG.println(tmpStr);
  lcd_display(F("SSID: OpenEVSE"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd_display(F("SSID: openevse"), 0, 1, 15 * 1000, LCD_CLEAR_LINE);

  apClients = 0;

  sync_mode();
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

  WiFi.hostname("openevse");
  WiFi.begin(esid.c_str(), epass.c_str());
  WiFi.enableSTA(true);

  clientState = Client_Connecting;

  sync_mode();
}

void wifi_onStationModeGotIP(const WiFiEventStationModeGotIP &event)
{
  #ifdef WIFI_LED
    wifiLedState = WIFI_LED_ON_STATE;
    digitalWrite(WIFI_LED, wifiLedState);
  #endif

  IPAddress myAddress = WiFi.localIP();
  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myAddress[0], myAddress[1], myAddress[2], myAddress[3]);
  ipaddress = tmpStr;
  DEBUG.print("Connected, IP: ");
  DEBUG.println(tmpStr);
  lcd_display(F("IP Address"), 0, 0, 0, LCD_CLEAR_LINE);
  lcd_display(tmpStr, 0, 1, 5000, LCD_CLEAR_LINE);
  // Copy the connected network and ipaddress to global strings for use in status request
  connected_network = esid;

  clientState = Client_Connected;
}

void wifi_onStationModeDisconnected(const WiFiEventStationModeDisconnected &event)
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

  clientState = WiFi.getMode() & WIFI_STA ? Client_Connecting : Client_Disconnected;
}

void
wifi_setup() {
#ifdef WIFI_LED
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, wifiLedState);
#endif

  randomSeed(analogRead(0));

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

  static auto _onStationModeConnected = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &event) { DBUGF("Connected to %s", event.ssid.c_str()); });
  static auto _onStationModeGotIP = WiFi.onStationModeGotIP(wifi_onStationModeGotIP);
  static auto _onStationModeDisconnected = WiFi.onStationModeDisconnected(wifi_onStationModeDisconnected);
  static auto _onSoftAPModeStationConnected = WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected &event) {
    lcd_display(F("IP Address"), 0, 0, 0, LCD_CLEAR_LINE);
    lcd_display(ipaddress, 0, 1, (0 == apClients ? 15 : 5) * 1000, LCD_CLEAR_LINE);
    apClients++;
  });
  static auto _onSoftAPModeStationDisconnected = WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected &event) {
    apClients--;
  });

  // 1) If no network configured start up access point
  if (esid == 0 || esid == "") {
    startAP();
  }
  // 2) else try and connect to the configured network
  else {
    startClient();
  }

  if (MDNS.begin(esp_hostname)) {
    MDNS.addService("http", "tcp", 80);
  }
}

void
wifi_loop() {
  Profile_Start(wifi_loop);

#ifdef WIFI_LED
  if ((WIFI_MODE_AP_ONLY == wifi_mode ||
       Client_Connecting == clientState) &&
      millis() > wifiLedTimeOut)
  {
    wifiLedState = !wifiLedState;
    digitalWrite(WIFI_LED, wifiLedState);

    int ledTime = Client_Connecting == clientState ? WIFI_LED_STA_CONNECTING_TIME :
                    0 == apClients ? WIFI_LED_AP_TIME : WIFI_LED_AP_CONNECTED_TIME;
    wifiLedTimeOut = millis() + ledTime;
  }
#endif

  // Manage the client connecting state
  switch(clientState)
  {
    case Client_Disconnected:
      break;

    case Client_Connecting:
      {
        // Pressing the boot button for 5 seconds while connecting will turn on AP mode
        #if !defined(WIFI_LED) || WIFI_BUTTON != WIFI_LED
          int button = digitalRead(WIFI_BUTTON);
          if(wifiButtonState != button) {
            wifiButtonState = button;
            if(LOW == button) {
              wifiButtonTimeOut = millis() + WIFI_BUTTON_TIMEOUT;
            }
          }

          if(LOW == wifiButtonState && millis() > wifiButtonTimeOut) {
            startAP();
          }
        #endif
      }
      break;

    case Client_Connected:
      break;

    case Client_Retry:
      break;
  }
/*
int t = 0;
int attempt = 0;
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  t++;
  // push and hold boot button after power on to skip stright to AP mode
  if (t >= 20
#if !defined(WIFI_LED) || 0 != WIFI_LED
     || digitalRead(0) == LOW
#endif
     )
  {
    DEBUG.println(" ");
    DEBUG.println("Try Again...");
    delay(2000);
    WiFi.disconnect();
    WiFi.begin(esid.c_str(), epass.c_str());
    t = 0;
    attempt++;
    if (attempt >= 5 || digitalRead(0) == LOW) {
      startAP();
      // AP mode with SSID in EEPROM, connection will retry in 5 minutes
      wifi_mode = WIFI_MODE_AP_STA_RETRY;
      break;
    }
  }
}

if (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_AP_AND_STA) {
}

*/

  dnsServer.processNextRequest(); // Captive portal DNS re-dierct

  // Remain in AP mode for 5 Minutes before resetting
  if (wifi_mode == WIFI_MODE_AP_STA_RETRY) {
    if ((millis() - Timer) >= 300000) {
      DEBUG.println("WIFI Mode = 1, resetting");
      delay(50);
      ESP.reset();
    }
  }

  Profile_End(wifi_loop, 5);
}

void
wifi_restart() {
  startClient();
}

void
wifi_disconnect() {
  WiFi.disconnect();
}

void wifi_turn_off_ap()
{
  if(WIFI_MODE_AP_AND_STA == wifi_mode)
  {
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    sync_mode();
  }
}

void wifi_turn_on_ap()
{
  DBUGF("wifi_turn_on_ap %d", wifi_mode);
  if(WIFI_MODE_STA == wifi_mode) {
    startAP();
  }
}

bool wifi_client_connected()
{
  return WiFi.isConnected() && (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_AP_AND_STA);
}
