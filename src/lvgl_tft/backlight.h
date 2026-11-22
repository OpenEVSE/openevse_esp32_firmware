#ifndef __LVGL_TFT_BACKLIGHT_H
#define __LVGL_TFT_BACKLIGHT_H

#include <stdint.h>

// Pure, host-testable backlight policy helpers (no Arduino/LVGL/openevse deps).

// Map a 0..100 brightness percentage to an 8-bit LEDC duty (0..255). Clamps >100.
uint8_t bl_pct_to_duty(uint8_t pct);

// Decide whether the panel should drop to standby.
//   keep_awake : caller-computed (charging / fault states force the screen bright)
//   timeout_s  : idle timeout in seconds; 0 == never sleep
//   idle_ms    : milliseconds since the last wake
bool bl_should_standby(bool keep_awake, uint32_t timeout_s, uint32_t idle_ms);

// Whether the status LEDs should be blanked: only while the panel is in
// standby and the owner has opted in with tft_standby_leds_off.
bool bl_leds_dark(bool standby, bool leds_off_in_standby);

// Whether the full-screen WiFi setup QR should own the display.
//   ap_mode    : the gateway is known to be in softAP mode (not a WiFi client)
//   qr_enabled : wifi_setup_prompt (the owner can turn the QR off for good)
//   vehicle    : a vehicle is connected
//   fault      : the EVSE is faulted, or the fault page is still being held
// A station with no WiFi at the site stays in AP mode indefinitely, so the QR
// gives way to the charge or fault screen whenever the charger is in use.
bool bl_show_setup_screen(bool ap_mode, bool qr_enabled, bool vehicle, bool fault);

#endif // __LVGL_TFT_BACKLIGHT_H
