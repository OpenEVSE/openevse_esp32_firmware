#ifndef _EMONESP_WIFI_H
#define _EMONESP_WIFI_H

#include <Arduino.h>

// Wifi mode
// 0 - STA (Client)
// 1 - AP with STA retry
// 2 - AP only
// 3 - AP + STA

#define WIFI_MODE_STA           0
#define WIFI_MODE_AP_STA_RETRY  1
#define WIFI_MODE_AP_ONLY       2
#define WIFI_MODE_AP_AND_STA    3

// The current WiFi mode
extern int wifi_mode;

// Last discovered WiFi access points
extern String st;
extern String rssi;

// Network state
extern String ipaddress;

extern void wifi_setup();
extern void wifi_loop();
extern void wifi_restart();
extern void wifi_scan();
extern void wifi_disconnect();

#endif // _EMONESP_WIFI_H
