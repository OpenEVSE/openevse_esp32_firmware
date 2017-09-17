#ifndef _EMONESP_WIFI_H
#define _EMONESP_WIFI_H

#include <Arduino.h>


// Last discovered WiFi access points
extern String st;
extern String rssi;

// Network state
extern String ipaddress;

// mDNS hostname
extern const char *esp_hostname;

extern void wifi_setup();
extern void wifi_loop();
extern void wifi_scan();

extern void wifi_restart();
extern void wifi_disconnect();

extern void wifi_turn_off_ap();
extern void wifi_turn_on_ap();
extern bool wifi_client_connected();

#define wifi_is_client_configured()   (WiFi.SSID() != "")

// Wifi mode
#define wifi_mode_is_sta()            (WIFI_STA == (WiFi.getMode() & WIFI_STA))
#define wifi_mode_is_sta_only()       (WIFI_STA == WiFi.getMode())
#define wifi_mode_is_ap()             (WIFI_AP == (WiFi.getMode() & WIFI_AP))

// Performing a scan enables STA so we end up in AP+STA mode so treat AP+STA with no
// ssid set as AP only
#define wifi_mode_is_ap_only()        ((WIFI_AP == WiFi.getMode()) || \
                                       (WIFI_AP_STA == WiFi.getMode() && !wifi_is_client_configured()))

#endif // _EMONESP_WIFI_H
