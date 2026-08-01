// src/lvgl_tft/setup_screen.h — WiFi-setup splash shown when the unit is in softAP
// mode (not yet configured). A QR code encodes a WiFi-join string so a phone can
// scan, join the AP, and open the setup page — no typing.
#ifndef __SETUP_SCREEN_H
#define __SETUP_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

// Build + load a dedicated LVGL screen with the QR + instructions.
//   qr_join : the WIFI:T:WPA;S:<ssid>;P:<pass>;; join string the QR encodes
//   ssid    : AP SSID, shown as text
//   pass    : AP password, shown as text (for manual entry)
//   ip      : AP IP (e.g. 192.168.4.1), shown as "or browse to ..."
void setup_screen_build(const char *qr_join, const char *ssid,
                        const char *pass, const char *ip);

// Delete the setup screen (call after another screen is loaded).
void setup_screen_destroy();

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __SETUP_SCREEN_H
