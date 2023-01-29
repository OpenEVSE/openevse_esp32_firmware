#ifndef _EMONESP_WIFI_H
#define _EMONESP_WIFI_H

#include <Arduino.h>
#include <MicroTasks.h>
#include <MicroTasksMessage.h>

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
#include "time_man.h"
#include <DNSServer.h>

class NetManagerTask;

class NetManagerTask : public MicroTasks::Task
{
  private:
    class NetState
    {
      public:
        enum Value : uint8_t {
          Starting,
          WiredConnecting,
          AccessPointConnecting,
          StationClientConnecting,
          StationClientReconnecting,
          Connected
        };

      NetState() = default;
      constexpr NetState(Value value) : _value(value) { }

      operator Value() const { return _value; }
      explicit operator bool() = delete;        // Prevent usage: if(state)
      NetState operator= (const Value val) {
        _value = val;
        return *this;
      }

      private:
        Value _value;
    };

    class NetMessage : public MicroTasks::Message
    {
      public:
        enum Value : uint32_t {
          WiFiStart,
          WiFiStop,
          WiFiRestart,
          WiFiAccessPointEnable,
          WiFiAccessPointDisable,
          NetworkEvent
        };

      NetMessage(Value value) : MicroTasks::Message(static_cast<uint32_t>(value)) { }

      operator Value() { return static_cast<Value>(id()); }
      explicit operator bool() = delete;        // Prevent usage: if(state)
    };

    class NetworkEventMessage : public NetMessage
    {
      private:
        WiFiEvent_t _event;
        arduino_event_info_t _info;
      public:
        NetworkEventMessage(WiFiEvent_t event, arduino_event_info_t info) :
          _event(event),
          _info(info),
          NetMessage(NetMessage::NetworkEvent)
        { }
        WiFiEvent_t event() { return _event; };
        arduino_event_info_t &info() { return _info; };
    };

    // Last discovered WiFi access points
    String _st;
    String _rssi;

    // Network state
    NetState _state;
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
    uint32_t _apAutoApStopTime;

    // Wifi Network Strings
    int _clientDisconnects;
    bool _clientRetry;
    unsigned long _clientRetryTime;

    int _wifiButtonState;
    unsigned long _wifiButtonTimeOut;
    bool _apMessage;

    #ifdef ENABLE_WIRED_ETHERNET
    bool _ethConnected;
    uint32_t _wiredTimeout;
    #endif

    LcdTask &_lcd;
    LedManagerTask &_led;
    TimeManager &_time;

    static class NetManagerTask *_instance;

  private:
    void wifiStartInternal();
    void wifiStopInternal();

    void wifiStartAccessPoint();
    void wifiStopAccessPoint();

    void wifiStartClient();
    void wifiStopClient();
    void wifiClientConnect();

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
    void onNetEvent(WiFiEvent_t event, arduino_event_info_t &info);
#endif

    unsigned long handleMessage();
    unsigned long serviceButton();
    unsigned long manageState();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    NetManagerTask(LcdTask &lcd, LedManagerTask &led, TimeManager &time);

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
