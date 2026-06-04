#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "fake_evse_core.h"
#include <vector>
#include <sstream>
#include <cstdio>

static std::vector<std::string> toks(const std::string &frame) {
  // strip leading nothing; cut at '^'; split on space
  std::string body = frame.substr(0, frame.find('^'));
  std::vector<std::string> out; std::istringstream is(body); std::string t;
  while (is >> t) out.push_back(t);
  return out;
}

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

TEST_CASE("$GV reports parseable firmware/protocol") {
  FakeEvseState st;
  auto t = toks(fake_evse_handle(st, "$GV"));
  REQUIRE(t.size() >= 3);
  CHECK(t[0] == "$OK");
  int a,b,c; CHECK(sscanf(t[2].c_str(), "%d.%d.%d", &a,&b,&c) == 3);
  int fa,fb,fc; CHECK(sscanf(t[1].c_str(), "%d.%d.%d", &fa,&fb,&fc) == 3);
}

TEST_CASE("$GS reports decimal state + elapsed") {
  FakeEvseState st; st.set_vehicle(true);          // -> charging (3)
  auto t = toks(fake_evse_handle(st, "$GS"));
  CHECK(t[0] == "$OK");
  CHECK(t[1] == "3");
  CHECK(t[2] == "0");
}

TEST_CASE("$SC sets pilot and echoes it; $GG follows charge state") {
  FakeEvseState st; st.set_vehicle(true);
  auto sc = toks(fake_evse_handle(st, "$SC 24"));
  CHECK(sc[0] == "$OK");
  CHECK(st.pilot_a == 24);
  auto gg = toks(fake_evse_handle(st, "$GG"));     // milliAmps milliVolts
  CHECK(gg[1] == "24000");
  CHECK(gg[2] == "240000");
}

TEST_CASE("$FS stops charge, $FE resumes") {
  FakeEvseState st; st.set_vehicle(true);
  fake_evse_handle(st, "$FS");
  CHECK(st.state() == FAKE_EVSE_SLEEPING);
  fake_evse_handle(st, "$FE");
  CHECK(st.state() == FAKE_EVSE_CHARGING);
}

TEST_CASE("$GF emits hex fault counters") {
  FakeEvseState st; st.gfci_count = 26; st.nognd_count = 0; st.stuck_count = 0;
  auto t = toks(fake_evse_handle(st, "$GF"));
  CHECK(t[0] == "$OK");
  CHECK(t[1] == "1A");
}

TEST_CASE("unknown command returns bare $OK") {
  FakeEvseState st;
  CHECK(fake_evse_handle(st, "$XYZ 1 2") == "$OK^20\r");
}
