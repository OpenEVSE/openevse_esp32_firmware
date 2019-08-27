#ifndef _EMONESP_WIFI_H
#define _EMONESP_WIFI_H

#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif

// Last discovered WiFi access points
extern String st;
extern String rssi;

// Network state
extern String ipaddress;

extern void net_setup();
extern void net_loop();
extern bool net_is_connected();

extern void net_wifi_scan();

extern void net_wifi_restart();
extern void net_wifi_disconnect();

extern void net_wifi_turn_off_ap();
extern void net_wifi_turn_on_ap();
extern bool net_wifi_client_connected();

#define net_wifi_is_client_configured()   (WiFi.SSID() != "")

// Wifi mode
#define net_wifi_mode_is_sta()            (WIFI_STA == (WiFi.getMode() & WIFI_STA))
#define net_wifi_mode_is_sta_only()       (WIFI_STA == WiFi.getMode())
#define net_wifi_mode_is_ap()             (WIFI_AP == (WiFi.getMode() & WIFI_AP))

// Performing a scan enables STA so we end up in AP+STA mode so treat AP+STA with no
// ssid set as AP only
#define net_wifi_mode_is_ap_only()        ((WIFI_AP == WiFi.getMode()) || \
                                       (WIFI_AP_STA == WiFi.getMode() && !net_wifi_is_client_configured()))


extern bool net_eth_connected();

#endif // _EMONESP_WIFI_H
