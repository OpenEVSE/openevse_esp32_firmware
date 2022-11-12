#ifndef _EMONESP_WIFI_H
#define _EMONESP_WIFI_H

#include <Arduino.h>
#include <MicroTasks.h>

#ifdef ESP32
#include <WiFi.h>
#include "wifi_esp32.h"
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif

#include "lcd.h"
#include "LedManagerTask.h"
#include <DNSServer.h>

class NetManagerTask;

class NetManagerTask : public MicroTasks::Task
{
  private:
    // Last discovered WiFi access points
    String _st;
    String _rssi;

    // Network state
    String _ipaddress;

    DNSServer _dnsServer;                  // Create class DNS server, captive portal re-direct
    bool _dnsServerStarted;
    const byte _dnsPort;

    // Access Point SSID, password & IP address. SSID will be softAP_ssid + chipID to make SSID unique
    const char *_softAP_ssid;
    const char *_softAP_password;
    IPAddress _apIP;
    IPAddress _apNetMask;
    int _apClients;

    // Wifi Network Strings
    int _clientDisconnects;
    bool _clientRetry;
    unsigned long _clientRetryTime;
    bool _clientConnecting;

    int _wifiButtonState;
    unsigned long _wifiButtonTimeOut;
    bool _apMessage;

    #ifdef ENABLE_WIRED_ETHERNET
    bool _eth_connected;
    #endif

    LcdTask &_lcd;
    LedManagerTask &_led;

    static class NetManagerTask *_instance;

  private:
    void wifiStartAccessPoint();
    void wifiStopAccessPoint();

    void wifiStartClient();
    void wifiStopClient();

    #ifdef ENABLE_WIRED_ETHERNET
    void wiredStart();
    void wiredStop();
    #endif

    void displayState();
    void haveNetworkConnection(IPAddress myAddress);

    void wifiOnStationModeConnected(const WiFiEventStationModeConnected &event);
    void wifiOnStationModeGotIP(const WiFiEventStationModeGotIP &event);
    void wifiOnStationModeDisconnected(const WiFiEventStationModeDisconnected &event);
    void wifiOnAPModeStationConnected(const WiFiEventSoftAPModeStationConnected &event);
    void wifiOnAPModeStationDisconnected(const WiFiEventSoftAPModeStationDisconnected &event);
#ifdef ESP32
    static void onNetEventStatic(WiFiEvent_t event, arduino_event_info_t info);
    void onNetEvent(WiFiEvent_t event, arduino_event_info_t info);
#endif

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    NetManagerTask(LcdTask &lcd, LedManagerTask &led);

    void begin();

    void wifiScan();

    void wifiStart();
    void wifiStop();
    void wifiRestart();

    void wifiTurnOffAp();
    void wifiTurnOnAp();

    bool isConnected();
    bool isWifiClientConnected();
    bool isWiredConnected();

    bool isWifiClientConfigured() {
      return (WiFi.SSID() != "");
    }

    bool isWifiModeSta() {
      return (WIFI_STA == (WiFi.getMode() & WIFI_STA));
    }

    bool isWifiModeStaOnly() {
      return (WIFI_STA == WiFi.getMode());
    }

    bool isWifiModeAp() {
      return (WIFI_AP == (WiFi.getMode() & WIFI_AP));
    }

    // Performing a scan enables STA so we end up in AP+STA mode so treat AP+STA with no
    // ssid set as AP only
    bool isWifiModeApOnly() {
      return ((WIFI_AP == WiFi.getMode()) ||
              (WIFI_AP_STA == WiFi.getMode() && !isWifiClientConfigured()));
    }

    String getIp() {
      return _ipaddress;
    }
};

extern NetManagerTask net;

#endif // _EMONESP_WIFI_H
