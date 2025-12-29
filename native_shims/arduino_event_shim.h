#pragma once

// Native-build shim for ESP32 Arduino WiFi event constants + event info struct.
// Used only for PlatformIO `native` builds.

#include <stdint.h>

// Provide the event constants referenced by src/net_manager.cpp.
// The numeric values don't matter for host builds; they only need to be
// compile-time constants.

enum {
  ARDUINO_EVENT_WIFI_READY = 1000,
  ARDUINO_EVENT_WIFI_SCAN_DONE,
  ARDUINO_EVENT_WIFI_STA_START,
  ARDUINO_EVENT_WIFI_STA_STOP,
  ARDUINO_EVENT_WIFI_STA_CONNECTED,
  ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
  ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE,
  ARDUINO_EVENT_WIFI_STA_GOT_IP,
  ARDUINO_EVENT_WIFI_STA_GOT_IP6,
  ARDUINO_EVENT_WIFI_STA_LOST_IP,
  ARDUINO_EVENT_WIFI_AP_START,
  ARDUINO_EVENT_WIFI_AP_STOP,
  ARDUINO_EVENT_WIFI_AP_STACONNECTED,
  ARDUINO_EVENT_WIFI_AP_STADISCONNECTED,
  ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED,
  ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED,
  ARDUINO_EVENT_WIFI_AP_GOT_IP6,
  ARDUINO_EVENT_WIFI_FTM_REPORT,

  ARDUINO_EVENT_ETH_START,
  ARDUINO_EVENT_ETH_STOP,
  ARDUINO_EVENT_ETH_CONNECTED,
  ARDUINO_EVENT_ETH_DISCONNECTED,
  ARDUINO_EVENT_ETH_GOT_IP,
  ARDUINO_EVENT_ETH_GOT_IP6,

  ARDUINO_EVENT_WPS_ER_SUCCESS,
  ARDUINO_EVENT_WPS_ER_FAILED,
  ARDUINO_EVENT_WPS_ER_TIMEOUT,
  ARDUINO_EVENT_WPS_ER_PIN,
  ARDUINO_EVENT_WPS_ER_PBC_OVERLAP,

  ARDUINO_EVENT_SC_SCAN_DONE,
  ARDUINO_EVENT_SC_FOUND_CHANNEL,
  ARDUINO_EVENT_SC_GOT_SSID_PSWD,
  ARDUINO_EVENT_SC_SEND_ACK_DONE,

  ARDUINO_EVENT_PROV_INIT,
  ARDUINO_EVENT_PROV_DEINIT,
  ARDUINO_EVENT_PROV_START,
  ARDUINO_EVENT_PROV_END,
  ARDUINO_EVENT_PROV_CRED_RECV,
  ARDUINO_EVENT_PROV_CRED_FAIL,
  ARDUINO_EVENT_PROV_CRED_SUCCESS,
};

// Provide the subset of arduino_event_info_t used by NetManagerTask.
// This intentionally only models the fields accessed in net_manager.cpp.

typedef struct {
  struct {
    uint8_t ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
  } wifi_sta_connected;

  struct {
    uint8_t ssid[33];
    uint8_t bssid[6];
    uint8_t reason;
  } wifi_sta_disconnected;

  struct {
    struct {
      struct { uint32_t addr; } ip;
      struct { uint32_t addr; } netmask;
      struct { uint32_t addr; } gw;
    } ip_info;
  } got_ip;

  struct {
    uint8_t mac[6];
    uint8_t aid;
  } wifi_ap_staconnected;

  struct {
    uint8_t mac[6];
    uint8_t aid;
  } wifi_ap_stadisconnected;

  struct {
    uint32_t number;
  } wifi_scan_done;
} arduino_event_info_t;
