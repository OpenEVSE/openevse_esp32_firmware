#ifndef __LED_POLICY_H
#define __LED_POLICY_H

// Pure, host-testable LED pattern policy (no Arduino deps).

// Priority of the normal EVSE-state pattern (a fault is raised far above it).
#define LED_EVSE_PRIORITY 50

// Priority of a WiFi status pattern (AP waiting / AP connected / STA connecting)
// against the EVSE-state pattern.
//   ap_mode : the gateway is running its own access point, not a WiFi client
//   vehicle : a vehicle is connected
//   prompt  : wifi_setup_prompt (the owner can silence the AP pattern for good)
// A charger with no WiFi at the site stays in AP mode indefinitely, so the WiFi
// pattern only wins while nobody is using the charger; the same rule decides
// when the TFT shows its setup QR.
inline int led_wifi_priority(bool ap_mode, bool vehicle, bool prompt)
{
  if(vehicle || (ap_mode && !prompt)) {
    return LED_EVSE_PRIORITY / 2;
  }
  return LED_EVSE_PRIORITY * 2;
}

#endif // __LED_POLICY_H
