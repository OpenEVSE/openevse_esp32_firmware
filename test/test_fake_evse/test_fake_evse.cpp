#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "fake_evse_core.h"

TEST_CASE("rapi checksum + frame match RapiSender expectation") {
  // RapiSender XORs all chars of the body; "$OK" = 0x24^0x4F^0x4B = 0x20
  CHECK(rapi_xor_checksum("$OK") == "20");
  // frame appends ^XX and CR
  CHECK(rapi_frame("$OK") == "$OK^20\r");
}

TEST_CASE("FakeEvseState state/charge_ma/set_vehicle") {
  FakeEvseState s;

  SUBCASE("default: no vehicle → NOT_CONNECTED, charge_ma==0") {
    CHECK(s.state()     == FAKE_EVSE_NOT_CONNECTED);
    CHECK(s.charge_ma() == 0);
  }

  SUBCASE("vehicle present + charging_allowed → CHARGING, charge_ma==32000") {
    s.set_vehicle(true);
    CHECK(s.state()     == FAKE_EVSE_CHARGING);
    CHECK(s.charge_ma() == 32000);   // pilot_a 32 × 1000
  }

  SUBCASE("vehicle present + charging_allowed false → SLEEPING, charge_ma==0") {
    s.set_vehicle(true);
    s.charging_allowed = false;
    CHECK(s.state()     == FAKE_EVSE_SLEEPING);
    CHECK(s.charge_ma() == 0);
  }

  SUBCASE("fault strings map to correct state codes (vehicle present; faults take priority)") {
    s.set_vehicle(true);

    s.fault = "gfci";
    CHECK(s.state() == FAKE_EVSE_GFI_FAULT);     // 6

    s.fault = "noground";
    CHECK(s.state() == FAKE_EVSE_NO_GROUND);      // 7

    s.fault = "stuck";
    CHECK(s.state() == FAKE_EVSE_STUCK_RELAY);    // 8

    s.fault = "overtemp";
    CHECK(s.state() == FAKE_EVSE_OVER_TEMP);      // 10
  }

  SUBCASE("set_vehicle rising edge resets session counters") {
    // start unplugged with accumulated session data
    s.vehicle_present   = false;
    s.session_elapsed_s = 100;
    s.session_wh        = 5.0;

    // rising edge: plug in
    s.set_vehicle(true);
    CHECK(s.session_elapsed_s == 0);
    CHECK(s.session_wh        == 0.0);
  }
}
