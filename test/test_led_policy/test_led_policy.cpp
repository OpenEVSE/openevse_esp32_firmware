#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "led_policy.h"

// The EVSE-state pattern sits at LED_EVSE_PRIORITY (faults far above it), so a
// WiFi pattern shows only when its priority beats that.
static bool wifiWins(bool ap_mode, bool vehicle, bool prompt) {
  return led_wifi_priority(ap_mode, vehicle, prompt) > LED_EVSE_PRIORITY;
}

// openevse-gui-nightshift#213: a charger with no WiFi at the site sits in AP
// mode for good, and the AP blink used to hide the charging pattern.
TEST_CASE("led_wifi_priority: AP blink shows on an idle charger") {
  CHECK(wifiWins(true, false, true));
}

TEST_CASE("led_wifi_priority: a connected vehicle gets the normal EVSE pattern") {
  CHECK_FALSE(wifiWins(true, true, true));
  CHECK_FALSE(wifiWins(false, true, true));   // STA reconnecting, too
}

TEST_CASE("led_wifi_priority: wifi_setup_prompt off silences the AP pattern") {
  CHECK_FALSE(wifiWins(true, false, false));
}

TEST_CASE("led_wifi_priority: STA connecting still shows on an idle charger") {
  CHECK(wifiWins(false, false, true));
  CHECK(wifiWins(false, false, false));       // the switch is about AP mode only
}
