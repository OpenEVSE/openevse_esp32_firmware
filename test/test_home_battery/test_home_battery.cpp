#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <ArduinoJson.h>
#include "home_battery.h"

// Each case resets the valid flags first, since the store is global state.

TEST_CASE("home_battery values omitted from /status until received") {
  home_battery_soc_valid = false;
  home_battery_power_valid = false;

  DynamicJsonDocument doc(256);
  home_battery_add_status_fields(doc);
  CHECK_FALSE(doc.containsKey("home_battery_soc"));
  CHECK_FALSE(doc.containsKey("home_battery_power"));
}

TEST_CASE("home_battery setters mark values valid and emit them") {
  home_battery_soc_valid = false;
  home_battery_power_valid = false;

  home_battery_set_soc(82);
  home_battery_set_power(-1500);   // negative = discharging

  DynamicJsonDocument doc(256);
  home_battery_add_status_fields(doc);
  CHECK(doc["home_battery_soc"].as<int>() == 82);
  CHECK(doc["home_battery_power"].as<int>() == -1500);
}

TEST_CASE("home_battery emits only the field that has been set") {
  home_battery_soc_valid = false;
  home_battery_power_valid = false;

  home_battery_set_soc(50);

  DynamicJsonDocument doc(256);
  home_battery_add_status_fields(doc);
  CHECK(doc["home_battery_soc"].as<int>() == 50);
  CHECK_FALSE(doc.containsKey("home_battery_power"));
}
