#include "emonesp.h"
#include "net_manager.h"
#include "app_config.h"
#include "lcd.h"
#include "espal.h"
#include "time_man.h"
#include "event.h"

#include "LedManagerTask.h"

#ifdef ESP32
#include <WiFi.h>
#include <esp_wifi.h>
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

#ifdef ESP32
#include "wifi_esp32.h"
#endif

#ifndef WIRED_CONNECT_TIMEOUT
#define WIRED_CONNECT_TIMEOUT (15 * 1000)
#endif

#ifndef ACCESS_POINT_AUTO_STOP_TIMEOUT
#define ACCESS_POINT_AUTO_STOP_TIMEOUT (10 * 1000)
#endif

NetManagerTask *NetManagerTask::_instance = NULL;

NetManagerTask::NetManagerTask(LcdTask &lcd, LedManagerTask &led, TimeManager &time) :
  _dnsServerStarted(false),
  _dnsPort(53),
  _softAP_ssid("OpenEVSE"),
  _softAP_password("openevse"),
  _apIP(192, 168, 4, 1),
  _apNetMask(255, 255, 255, 0),
  _apClients(0),
  _state(NetState::Starting),
  _ipaddress(""),
  _macaddress(""),
  _clientDisconnects(0),
  _clientRetry(false),
  _clientRetryTime(0),
  _wifiButtonState(!WIFI_BUTTON_PRESSED_STATE),
  _wifiButtonTimeOut(millis()),
  _apMessage(false),
  #ifdef ENABLE_WIRED_ETHERNET
  _ethConnected(false),
  #endif
  _lcd(lcd),
  _led(led),
  _time(time)
{
}

void NetManagerTask::begin()
{
  if(NULL == _instance)
  {
    _instance = this;
    MicroTask.startTask(this);
  }
}

// -------------------------------------------------------------------
// Start Access Point
// Access point is used for wifi network selection
// -------------------------------------------------------------------
void NetManagerTask::wifiStartAccessPoint()
{
  DBUGLN("Starting AP");

  DBUGVAR(isWifiModeAp());
  DBUGVAR(isWifiModeSta());

  if((esid == 0 || esid == "") && isWifiModeSta()) {
    WiFi.disconnect(true);
  }

  WiFi.enableAP(true);
  WiFi.enableSTA(true); // Needed for scanning
  WiFi.softAPConfig(_apIP, _apIP, _apNetMask);
  // set country code to "world safe mode"
  esp_wifi_set_country_code("01", true);

  String softAP_ssid;

  if (ap_ssid.length() < 2) {
     // Create Unique SSID e.g "OpenEVSE_XXXXXX"
    softAP_ssid = String(_softAP_ssid) + "_" + ESPAL.getShortId();
  }
  else {
    softAP_ssid = ap_ssid;
  }

  // Use the existing channel if set
  int channel = WiFi.channel();
  DBUGVAR(channel);
  if(0 == channel || esid == 0 || esid == "") {
    // Pick a random channel out of 1, 6 or 11
    channel = (random(3) * 5) + 1;
  }
  DBUGVAR(channel);
  WiFi.softAP(softAP_ssid.c_str(), ap_pass.length()>=8?ap_pass.c_str():_softAP_password, channel);

  // Setup the DNS server redirecting all the domains to the apIP
  _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServerStarted = _dnsServer.start(_dnsPort, "*", _apIP);

  IPAddress myIP = WiFi.softAPIP();

  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
  _ipaddress = tmpStr;
  _macaddress = WiFi.macAddress();

  DEBUG.printf("AP IP Address: %s\n", tmpStr);
  DEBUG.printf("Channel: %d\n", WiFi.channel());

  _lcd.display(softAP_ssid, 0, 0, 0, LCD_CLEAR_LINE);
  _lcd.display(String(F("Pass: ")) + _softAP_password, 0, 1, 15 * 1000, LCD_CLEAR_LINE);

  _led.setWifiMode(false, false);
  _lcd.setWifiMode(false, false);
  _apClients = 0;
  _state = NetState::AccessPointConnecting;
}

void NetManagerTask::wifiStopAccessPoint()
{
  WiFi.softAPdisconnect(true);
  _dnsServer.stop();
  _dnsServerStarted = false;
}

// -------------------------------------------------------------------
// Start Client, attempt to connect to Wifi network
// -------------------------------------------------------------------
void NetManagerTask::wifiStartClient()
{
  wifiClientConnect();

  _led.setWifiMode(true, false);
  _lcd.setWifiMode(true, false);
  _state = NetState::StationClientConnecting;
}

void NetManagerTask::wifiClientConnect()
{
  DEBUG.print("Connecting to SSID: ");
  DEBUG.println(esid.c_str());
  // DEBUG.print(" epass:");
  // DEBUG.println(epass.c_str());

  WiFi.hostname(esp_hostname.c_str());
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(esid.c_str(), epass.c_str());

  _clientRetryTime = millis() + WIFI_CLIENT_RETRY_TIMEOUT;
}

void NetManagerTask::wifiScanNetworks(WiFiScanCompleteCallback callback)
{
  if(WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanNetworks(true, false, false);
  }
  _scanCompleteCallbacks.push_back(callback);
}

void NetManagerTask::displayState()
{
  _lcd.display(F("Hostname:"), 0, 0, 0, LCD_CLEAR_LINE);
  _lcd.display(esp_hostname.c_str(), 0, 1, 5000, LCD_CLEAR_LINE);

  _lcd.display(F("IP Address:"), 0, 0, 0, LCD_CLEAR_LINE);
  _lcd.display(_ipaddress.c_str(), 0, 1, 5000, LCD_CLEAR_LINE);
}

void NetManagerTask::haveNetworkConnection(IPAddress myAddress)
{
  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myAddress[0], myAddress[1], myAddress[2], myAddress[3]);
  _ipaddress = tmpStr;
  _macaddress = WiFi.macAddress();

  DEBUG.print("Connected, IP: ");
  DEBUG.println(tmpStr);

  displayState();

  Mongoose.ipConfigChanged();

  _led.setWifiMode(true, true);
  _lcd.setWifiMode(true, true);
  _time.setHost(sntp_hostname.c_str());

  _apAutoApStopTime = millis() + ACCESS_POINT_AUTO_STOP_TIMEOUT;

  _state = NetState::Connected;
}

void NetManagerTask::wifiOnStationModeConnected(const WiFiEventStationModeConnected &event) {
  DBUGF("Connected to %s", event.ssid.c_str());
}

void NetManagerTask::wifiOnStationModeGotIP(const WiFiEventStationModeGotIP &event)
{
  haveNetworkConnection(WiFi.localIP());
  _macaddress = WiFi.macAddress();
  StaticJsonDocument<128> doc;
  doc["wifi_client_connected"] = (int)net.isWifiClientConnected();
  doc["eth_connected"] = (int)net.isWiredConnected();
  doc["net_connected"] = (int)net.isWifiClientConnected();
  doc["ipaddress"] = net.getIp();
  doc["macaddress"] = net.getMac();
  event_send(doc);

  // Clear any error state
  _clientDisconnects = 0;
}

void NetManagerTask::wifiOnStationModeDisconnected(const WiFiEventStationModeDisconnected &event)
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

  _clientDisconnects++;

  // Clear the WiFi state and try to connect again
  WiFi.disconnect(true);

  if(!isWiredConnected() && NetState::Connected == _state) {
    wifiStart();
  }
}

void NetManagerTask::wifiOnAPModeStationConnected(const WiFiEventSoftAPModeStationConnected &event)
{
  _lcd.display(F("IP Address"), 0, 0, 0, LCD_CLEAR_LINE);
  _lcd.display(_ipaddress, 0, 1, (0 == _apClients ? 15 : 5) * 1000, LCD_CLEAR_LINE);

  _led.setWifiMode(false, true);
  _lcd.setWifiMode(false, true);
  _apClients++;
}

void NetManagerTask::wifiOnAPModeStationDisconnected(const WiFiEventSoftAPModeStationDisconnected &event)
{
  _apClients--;

  if(0 == _apClients && NetState::AccessPointConnecting == _state) {
    _led.setWifiMode(false, false);
    _lcd.setWifiMode(false, false);
  }
}

#ifdef ESP32

void NetManagerTask::onNetEventStatic(WiFiEvent_t event, arduino_event_info_t info)
{
  if(_instance) {
    _instance->send(new NetworkEventMessage(event, info));
  }
}

void NetManagerTask::onNetEvent(WiFiEvent_t event, arduino_event_info_t &info)
{
  DBUGF("Got Network event %s",
    ARDUINO_EVENT_WIFI_READY == event ? "ARDUINO_EVENT_WIFI_READY" :
    ARDUINO_EVENT_WIFI_SCAN_DONE == event ? "ARDUINO_EVENT_WIFI_SCAN_DONE" :
    ARDUINO_EVENT_WIFI_STA_START == event ? "ARDUINO_EVENT_WIFI_STA_START" :
    ARDUINO_EVENT_WIFI_STA_STOP == event ? "ARDUINO_EVENT_WIFI_STA_STOP" :
    ARDUINO_EVENT_WIFI_STA_CONNECTED == event ? "ARDUINO_EVENT_WIFI_STA_CONNECTED" :
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED == event ? "ARDUINO_EVENT_WIFI_STA_DISCONNECTED" :
    ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE == event ? "ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE" :
    ARDUINO_EVENT_WIFI_STA_GOT_IP == event ? "ARDUINO_EVENT_WIFI_STA_GOT_IP" :
    ARDUINO_EVENT_WIFI_STA_GOT_IP6 == event ? "ARDUINO_EVENT_WIFI_STA_GOT_IP6" :
    ARDUINO_EVENT_WIFI_STA_LOST_IP == event ? "ARDUINO_EVENT_WIFI_STA_LOST_IP" :
    ARDUINO_EVENT_WIFI_AP_START == event ? "ARDUINO_EVENT_WIFI_AP_START" :
    ARDUINO_EVENT_WIFI_AP_STOP == event ? "ARDUINO_EVENT_WIFI_AP_STOP" :
    ARDUINO_EVENT_WIFI_AP_STACONNECTED == event ? "ARDUINO_EVENT_WIFI_AP_STACONNECTED" :
    ARDUINO_EVENT_WIFI_AP_STADISCONNECTED == event ? "ARDUINO_EVENT_WIFI_AP_STADISCONNECTED" :
    ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED == event ? "ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED" :
    ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED == event ? "ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED" :
    ARDUINO_EVENT_WIFI_AP_GOT_IP6 == event ? "ARDUINO_EVENT_WIFI_AP_GOT_IP6" :
    ARDUINO_EVENT_WIFI_FTM_REPORT == event ? "ARDUINO_EVENT_WIFI_FTM_REPORT" :
    ARDUINO_EVENT_ETH_START == event ? "ARDUINO_EVENT_ETH_START" :
    ARDUINO_EVENT_ETH_STOP == event ? "ARDUINO_EVENT_ETH_STOP" :
    ARDUINO_EVENT_ETH_CONNECTED == event ? "ARDUINO_EVENT_ETH_CONNECTED" :
    ARDUINO_EVENT_ETH_DISCONNECTED == event ? "ARDUINO_EVENT_ETH_DISCONNECTED" :
    ARDUINO_EVENT_ETH_GOT_IP == event ? "ARDUINO_EVENT_ETH_GOT_IP" :
    ARDUINO_EVENT_ETH_GOT_IP6 == event ? "ARDUINO_EVENT_ETH_GOT_IP6" :
    ARDUINO_EVENT_WPS_ER_SUCCESS == event ? "ARDUINO_EVENT_WPS_ER_SUCCESS" :
    ARDUINO_EVENT_WPS_ER_FAILED == event ? "ARDUINO_EVENT_WPS_ER_FAILED" :
    ARDUINO_EVENT_WPS_ER_TIMEOUT == event ? "ARDUINO_EVENT_WPS_ER_TIMEOUT" :
    ARDUINO_EVENT_WPS_ER_PIN == event ? "ARDUINO_EVENT_WPS_ER_PIN" :
    ARDUINO_EVENT_WPS_ER_PBC_OVERLAP == event ? "ARDUINO_EVENT_WPS_ER_PBC_OVERLAP" :
    ARDUINO_EVENT_SC_SCAN_DONE == event ? "ARDUINO_EVENT_SC_SCAN_DONE" :
    ARDUINO_EVENT_SC_FOUND_CHANNEL == event ? "ARDUINO_EVENT_SC_FOUND_CHANNEL" :
    ARDUINO_EVENT_SC_GOT_SSID_PSWD == event ? "ARDUINO_EVENT_SC_GOT_SSID_PSWD" :
    ARDUINO_EVENT_SC_SEND_ACK_DONE == event ? "ARDUINO_EVENT_SC_SEND_ACK_DONE" :
    ARDUINO_EVENT_PROV_INIT == event ? "ARDUINO_EVENT_PROV_INIT" :
    ARDUINO_EVENT_PROV_DEINIT == event ? "ARDUINO_EVENT_PROV_DEINIT" :
    ARDUINO_EVENT_PROV_START == event ? "ARDUINO_EVENT_PROV_START" :
    ARDUINO_EVENT_PROV_END == event ? "ARDUINO_EVENT_PROV_END" :
    ARDUINO_EVENT_PROV_CRED_RECV == event ? "ARDUINO_EVENT_PROV_CRED_RECV" :
    ARDUINO_EVENT_PROV_CRED_FAIL == event ? "ARDUINO_EVENT_PROV_CRED_FAIL" :
    ARDUINO_EVENT_PROV_CRED_SUCCESS == event ? "ARDUINO_EVENT_PROV_CRED_SUCCESS" :
    "UNKNOWN"
  );

  DBUGF("WiFi State %s",
        NetState::Starting == _state ? "Starting" :
        NetState::WiredConnecting == _state ? "WiredConnecting" :
        NetState::AccessPointConnecting == _state ? "AccessPointConnecting" :
        NetState::StationClientConnecting == _state ? "StationClientConnecting" :
        NetState::StationClientReconnecting == _state ? "StationClientReconnecting" :
        NetState::Connected == _state ? "Connected" :
        "UNKNOWN");

  //WiFi.printDiag(DEBUG_PORT);
  wifi_mode_t mode = WiFi.getMode();
  DBUGF("WiFi Mode %s",
        WIFI_OFF == mode ? "OFF" :
        WIFI_STA == mode ? "STA" :
        WIFI_AP == mode ? "AP" :
        WIFI_AP_STA == mode ? "AP+STA" :
        "UNKNOWN");

  switch (event)
  {
    case ARDUINO_EVENT_WIFI_AP_START:
    {
      if(WiFi.softAPsetHostname(esp_hostname.c_str())) {
        DBUGF("Set host name to %s", WiFi.softAPgetHostname());
      } else {
        DBUGF("Setting host name failed: %s", esp_hostname.c_str());
      }
    } break;

    case ARDUINO_EVENT_WIFI_STA_START:
    {
      if(WiFi.setHostname(esp_hostname.c_str())) {
        DBUGF("Set host name to %s", WiFi.getHostname());
      } else {
        DBUGF("Setting host name failed: %s", esp_hostname.c_str());
      }
    } break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    {
      auto& src = info.wifi_sta_connected;
      WiFiEventStationModeConnected dst;
      dst.ssid = String(reinterpret_cast<char*>(src.ssid));
      memcpy(dst.bssid, src.bssid, 6);
      dst.channel = src.channel;
      wifiOnStationModeConnected(dst);
    } break;

    case ARDUINO_EVENT_WIFI_STA_STOP:
    {
    } break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    {
      auto& src = info.wifi_sta_disconnected;
      WiFiEventStationModeDisconnected dst;
      dst.ssid = String(reinterpret_cast<char*>(src.ssid));
      memcpy(dst.bssid, src.bssid, 6);
      dst.reason = static_cast<WiFiDisconnectReason>(src.reason);
      wifiOnStationModeDisconnected(dst);
    } break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    {
      auto& src = info.got_ip.ip_info;
      WiFiEventStationModeGotIP dst;
      dst.ip = src.ip.addr;
      dst.mask = src.netmask.addr;
      dst.gw = src.gw.addr;
      wifiOnStationModeGotIP(dst);
    } break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    {
      auto& src = info.wifi_ap_staconnected;
      WiFiEventSoftAPModeStationConnected dst;
      memcpy(dst.mac, src.mac, 6);
      dst.aid = src.aid;
      wifiOnAPModeStationConnected(dst);
    } break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    {
      auto& src = info.wifi_ap_stadisconnected;
      WiFiEventSoftAPModeStationDisconnected dst;
      memcpy(dst.mac, src.mac, 6);
      dst.aid = src.aid;
      wifiOnAPModeStationDisconnected(dst);
    } break;

    case ARDUINO_EVENT_WIFI_SCAN_DONE:
    {
      for(WiFiScanCompleteCallback callback : _scanCompleteCallbacks) {
        callback(info.wifi_scan_done.number);
      }

      _scanCompleteCallbacks.clear();
      WiFi.scanDelete();
    } break;
#ifdef ENABLE_WIRED_ETHERNET
    case ARDUINO_EVENT_ETH_START:
      DBUGF("ETH Started, link %s", ETH.linkUp() ? "up" : "down");
      if(ETH.linkUp())
      {
        //set eth hostname here
        if(ETH.setHostname(esp_hostname.c_str())) {
          DBUGF("Set host name to %s", ETH.getHostname());
        } else {
          DBUGF("Setting host name failed: %s", esp_hostname.c_str());
        }
      } else {
        wifiStartInternal();
      }
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      DBUGLN("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
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
      haveNetworkConnection(ETH.localIP());
      _macaddress = ETH.macAddress();
      _ethConnected = true;
      wifiStop();
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      DBUGLN("ETH Disconnected");
      _ethConnected = false;
      wifiStart();
      break;
    case ARDUINO_EVENT_ETH_STOP:
      DBUGLN("ETH Stopped");
      _ethConnected = false;
      break;
#endif
    default:
      break;
  }
}
#endif

void NetManagerTask::setup()
{
  DBUGLN("Starting Network Manager");

  // This is not really needed for our usecase on the ESP32, because random(n) uses theESP32 hardware random number
  // generator, but random() does not so safest to add some entropy here
  randomSeed(millis() | analogRead(RANDOM_SEED_CHANNEL));

  // If we have an SSID configured at this point we have likely
  // been running another firmware, clear the results
  if(isWifiClientConfigured()) {
    WiFi.persistent(true);
    WiFi.disconnect();
    ESPAL.eraseConfig();
  }

  // Stop the WiFi module
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

#ifdef ESP32
  WiFi.onEvent(onNetEventStatic);
#else
// TODO: Needs fixing if we ever support ESP8622
//  static auto _onStationModeConnected = WiFi.onStationModeConnected(net_wifi_onStationModeConnected);
//  static auto _onStationModeGotIP = WiFi.onStationModeGotIP(net_wifi_onStationModeGotIP);
//  static auto _onStationModeDisconnected = WiFi.onStationModeDisconnected(net_wifi_onStationModeDisconnected);
//  static auto _onSoftAPModeStationConnected = WiFi.onSoftAPModeStationConnected(net_wifi_onAPModeStationConnected);
//  static auto _onSoftAPModeStationDisconnected = WiFi.onSoftAPModeStationDisconnected(net_wifi_onAPModeStationDisconnected);
#endif

  // Initially startup the netwrok to kick things off
  manageState();

  if (MDNS.begin(esp_hostname.c_str()))
  {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("openevse", "tcp", 80);
    MDNS.addServiceTxt("openevse", "tcp", "type", buildenv.c_str());
    MDNS.addServiceTxt("openevse", "tcp", "version", currentfirmware.c_str());
    MDNS.addServiceTxt("openevse", "tcp", "id", ESPAL.getLongId());
    
  }
}

unsigned long NetManagerTask::handleMessage()
{
  MicroTasks::Message *msg;
  if(this->receive(msg))
  {
    switch (msg->id())
    {
      case NetMessage::WiFiRestart:
      case NetMessage::WiFiStop:
        wifiStopInternal();
        if(msg->id() == NetMessage::WiFiStop) { break; };

      case NetMessage::WiFiStart:
        wifiStartInternal();
        break;

      case NetMessage::WiFiAccessPointEnable:
        wifiStartAccessPoint();
        break;

      case NetMessage::WiFiAccessPointDisable:
        wifiStopAccessPoint();
        break;

      case NetMessage::NetworkEvent:
      {
        NetworkEventMessage *netEvent = static_cast<NetworkEventMessage *>(msg);
        onNetEvent(netEvent->event(), netEvent->info());
      }break;

      default:
        break;
    }

    delete msg;
  }

  return MicroTask.Infinate;
}

unsigned long NetManagerTask::serviceButton()
{
  int button = _led.getButtonPressed();

  //DBUGF("%lu %d %d", millis() - _wifiButtonTimeOut, button, _wifiButtonState);
  if(_wifiButtonState != button)
  {
    _wifiButtonState = button;
    if(WIFI_BUTTON_PRESSED_STATE == button) {
      DBUGF("Button pressed");
      _wifiButtonTimeOut = millis();
      _apMessage = false;
    } else {
      DBUGF("Button released");
      if(millis() > _wifiButtonTimeOut + WIFI_BUTTON_AP_TIMEOUT) {
        wifiStartAccessPoint();
      } else {
        displayState();
      }
    }
  }

  if(WIFI_BUTTON_PRESSED_STATE == _wifiButtonState && millis() > _wifiButtonTimeOut + WIFI_BUTTON_FACTORY_RESET_TIMEOUT)
  {
    DBUGLN("*** Factory Reset ***");

    _lcd.display(F("Factory Reset"), 0, 0, 0, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    _lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);

    delay(1000);

    config_reset();
    ESPAL.eraseConfig();

    delay(50);
    ESPAL.reset();
  }
  else if(false == _apMessage && LOW == _wifiButtonState && millis() > _wifiButtonTimeOut + WIFI_BUTTON_AP_TIMEOUT)
  {
    DBUGLN("*** Enable Access Point ***");

    _lcd.display(F("Access Point"), 0, 0, 0, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    _lcd.display(F(""), 0, 1, 10 * 1000, LCD_CLEAR_LINE | LCD_DISPLAY_NOW);
    _apMessage = true;
  }

  return 0;
}

unsigned long NetManagerTask::manageState()
{
  unsigned long delayTime = MicroTask.Infinate;

  switch (_state)
  {
    case NetState::Starting:
      #ifdef ENABLE_WIRED_ETHERNET
      wiredStart();
      #else
      wifiStartInternal();
      #endif
      break;
#ifdef ENABLE_WIRED_ETHERNET
    case NetState::WiredConnecting:
      if(millis() > _wiredTimeout) {
        DBUGF("Wired connection timed out");
        wifiStartInternal();
      } else {
        delayTime = _wiredTimeout - millis();
      }
      break;
#endif
    case NetState::StationClientConnecting:
      if(_clientDisconnects > WIFI_CLIENT_DISCONNECTS_BEFORE_AP) {
        wifiStartAccessPoint();
      }
      // Intentionally fall through to AP State for the same client reconnect logic
    case NetState::AccessPointConnecting:
      if(!isWifiClientConnected() && esid != 0 && esid != "" && millis() > _clientRetryTime) {
        wifiClientConnect();
      }

      delayTime = _clientRetryTime - millis();
      break;
    case NetState::StationClientReconnecting:
      break;
    case NetState::Connected:
      if(millis() > _apAutoApStopTime)
      {
        if(isWifiModeAp()) {
          wifiStopAccessPoint();
        }
      } else {
        delayTime = _apAutoApStopTime - millis();
      }
      break;
  }

  return delayTime;
}

unsigned long NetManagerTask::loop(MicroTasks::WakeReason reason)
{
  unsigned long nextLoopDelay = MicroTask.Infinate;

  Profile_Start(NetManagerTask::loop);

//  DBUG("NetManagerTask woke: ");
//  DBUG(NetState::Starting == _state ? "Starting" :
//       NetState::WiredConnecting == _state ? "WiredConnecting" :
//       NetState::AccessPointConnecting == _state ? "AccessPointConnecting" :
//       NetState::StationClientConnecting == _state ? "StationClientConnecting" :
//       NetState::StationClientReconnecting == _state ? "StationClientReconnecting" :
//       NetState::Connected == _state ? "Connected" :
//       "UNKNOWN");
//  DBUG(", ");
//  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
//         WakeReason_Event == reason ? "WakeReason_Event" :
//         WakeReason_Message == reason ? "WakeReason_Message" :
//         WakeReason_Manual == reason ? "WakeReason_Manual" :
//         "UNKNOWN");

  if(WakeReason_Message == reason) {
    nextLoopDelay = min(handleMessage(), nextLoopDelay);
  }

  nextLoopDelay = min(serviceButton(), nextLoopDelay);

  nextLoopDelay = min(manageState(), nextLoopDelay);

  if(_dnsServerStarted) {
    _dnsServer.processNextRequest(); // Captive portal DNS re-dierct
  }

  Profile_End(NetManagerTask::loop, 5);

  return nextLoopDelay;
}

#ifdef ENABLE_WIRED_ETHERNET
void NetManagerTask::wiredStart()
{
  DBUGLN("Starting wired connection");
  //ETH.setHostname(esp_hostname.c_str());
  // https://github.com/espressif/arduino-esp32/pull/6188/files
  pinMode(ETH_PHY_POWER, OUTPUT);
#ifdef RESET_ETH_PHY_ON_BOOT
  digitalWrite(ETH_PHY_POWER, LOW);
  delay(350);
#endif // #ifdef RESET_ETH_PHY_ON_BOOT
  digitalWrite(ETH_PHY_POWER, HIGH);
  ETH.begin();

  _state = NetState::WiredConnecting;
  _wiredTimeout = millis() + WIRED_CONNECT_TIMEOUT;
}
#endif

void NetManagerTask::wifiStartInternal()
{
  _clientDisconnects = 0;

  // 1) If no network configured start up access point
  if (esid == 0 || esid == "")
  {
    wifiStartAccessPoint();
  }
  // 2) else try and connect to the configured network
  else
  {
    wifiStartClient();
  }
}

void NetManagerTask::wifiStopInternal()
{
  wifiTurnOffAp();
  if (isWifiModeSta()) {
    WiFi.disconnect(true);
  }
}

void NetManagerTask::wifiStart()
{
  DBUGF("NetManagerTask::wifiStart %d", WiFi.getMode());
  send(new NetMessage(NetMessage::WiFiStart));
}

void NetManagerTask::wifiStop()
{
  DBUGF("NetManagerTask::wifiStop %d", WiFi.getMode());
  send(new NetMessage(NetMessage::WiFiStop));
}

void NetManagerTask::wifiRestart()
{
  DBUGF("NetManagerTask::wifiRestart %d", WiFi.getMode());
  send(new NetMessage(NetMessage::WiFiRestart));
}

void NetManagerTask::wifiTurnOffAp()
{
  DBUGF("NetManagerTask::wifiTurnOffAp %d", WiFi.getMode());
  if(isWifiModeAp()) {
    send(new NetMessage(NetMessage::WiFiAccessPointDisable));
  }
}

void NetManagerTask::wifiTurnOnAp()
{
  DBUGF("NetManagerTask::wifiTurnOnAp %d", WiFi.getMode());
  if(!isWifiModeAp()) {
    send(new NetMessage(NetMessage::WiFiAccessPointEnable));
  }
}

bool NetManagerTask::isConnected()
{
  return isWifiClientConnected() || isWiredConnected();
}

bool NetManagerTask::isWifiClientConnected()
{
  return WiFi.isConnected() && isWifiModeSta();
}

bool NetManagerTask::isWiredConnected()
{
#ifdef ENABLE_WIRED_ETHERNET
  return _ethConnected;
#else
  return false;
#endif
}