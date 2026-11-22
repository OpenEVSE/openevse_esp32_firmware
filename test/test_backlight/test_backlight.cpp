#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "backlight.h"

TEST_CASE("bl_pct_to_duty maps 0..100 to 0..255") {
  CHECK(bl_pct_to_duty(0) == 0);
  CHECK(bl_pct_to_duty(100) == 255);
  CHECK(bl_pct_to_duty(10) == 26);    // 10*2.55 = 25.5 -> rounds up to 26
  CHECK(bl_pct_to_duty(50) == 128);   // 50*2.55 = 127.5 -> rounds up to 128
  CHECK(bl_pct_to_duty(200) == 255);  // over-range clamps
  for (uint8_t p = 1; p <= 100; ++p) {
    CHECK(bl_pct_to_duty(p) >= bl_pct_to_duty((uint8_t)(p - 1)));
  }
}

TEST_CASE("bl_should_standby: keep_awake never sleeps") {
  CHECK_FALSE(bl_should_standby(true, 600, 999999));
}

TEST_CASE("bl_should_standby: timeout 0 means never") {
  CHECK_FALSE(bl_should_standby(false, 0, 999999));
}

TEST_CASE("bl_should_standby: sleeps only once idle reaches the timeout") {
  CHECK_FALSE(bl_should_standby(false, 600, 599999));
  CHECK(bl_should_standby(false, 600, 600000));
  CHECK(bl_should_standby(false, 600, 600001));
}

TEST_CASE("bl_leds_dark: LEDs go dark only in standby with the option on") {
  CHECK(bl_leds_dark(true, true));
  CHECK_FALSE(bl_leds_dark(true, false));   // option off: existing behaviour
  CHECK_FALSE(bl_leds_dark(false, true));   // awake: LEDs follow the EVSE state
  CHECK_FALSE(bl_leds_dark(false, false));
}

// openevse-gui-nightshift#213: a station with no WiFi sits in AP mode for good,
// so the setup QR must not hold the screen once someone is actually using it.
TEST_CASE("bl_show_setup_screen: QR only in AP mode, unplugged, no fault, option on") {
  CHECK(bl_show_setup_screen(true, true, false, false));
}

TEST_CASE("bl_show_setup_screen: never outside AP mode") {
  CHECK_FALSE(bl_show_setup_screen(false, true, false, false));
}

TEST_CASE("bl_show_setup_screen: a connected vehicle gets the charge screen") {
  CHECK_FALSE(bl_show_setup_screen(true, true, true, false));
}

TEST_CASE("bl_show_setup_screen: a fault gets the fault screen") {
  CHECK_FALSE(bl_show_setup_screen(true, true, false, true));
  CHECK_FALSE(bl_show_setup_screen(true, true, true, true));
}

TEST_CASE("bl_show_setup_screen: tft_setup_qr off suppresses it entirely") {
  CHECK_FALSE(bl_show_setup_screen(true, false, false, false));
}
