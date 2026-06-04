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

TEST_CASE("$SC clamps pilot to capacity range") {
  FakeEvseState st;                 // min_a=6, max_cfg_a=32
  fake_evse_handle(st, "$SC 99");   CHECK(st.pilot_a == 32);   // clamped high
  fake_evse_handle(st, "$SC 1");    CHECK(st.pilot_a == 6);    // clamped low
  fake_evse_handle(st, "$SC 16");   CHECK(st.pilot_a == 16);   // in range
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

TEST_CASE("tick accrues energy only while charging") {
  FakeEvseState st; st.set_vehicle(true);     // charging, pilot 32A @ 240V
  fake_evse_tick(st, 3600.0);                 // 1 hour
  CHECK(st.session_elapsed_s == 3600);
  CHECK(st.session_wh == doctest::Approx(32.0 * 240.0)); // 7680 Wh
  CHECK(st.total_wh   == doctest::Approx(7680.0));
  // sleep -> no further accrual
  fake_evse_handle(st, "$FS");
  fake_evse_tick(st, 3600.0);
  CHECK(st.session_elapsed_s == 3600);        // unchanged
}

TEST_CASE("tick emits $ST on transition with hex state") {
  FakeEvseState st;                            // state 1
  CHECK(fake_evse_tick(st, 1.0) == "");        // no change
  st.set_vehicle(true);                        // -> 3 (charging)
  std::string ev = fake_evse_tick(st, 1.0);
  CHECK(ev == rapi_frame("$ST 03"));           // 3 in 2-hex
  CHECK(fake_evse_tick(st, 1.0) == "");        // no re-emit once reported
  st.set_vehicle(false);                       // unplug -> 1
  CHECK(fake_evse_tick(st, 1.0) == rapi_frame("$ST 01"));
}

TEST_CASE("total_wh persists across replug; session resets") {
  FakeEvseState st; st.set_vehicle(true);
  fake_evse_tick(st, 3600.0);                  // ~7680 Wh
  double total_after_first = st.total_wh;
  CHECK(total_after_first == doctest::Approx(7680.0));
  st.set_vehicle(false);                       // unplug
  st.set_vehicle(true);                        // replug -> new session
  CHECK(st.session_wh == doctest::Approx(0.0));
  CHECK(st.session_elapsed_s == 0);
  fake_evse_tick(st, 3600.0);                  // another ~7680 Wh
  CHECK(st.total_wh == doctest::Approx(15360.0));   // accumulates
  CHECK(st.session_wh == doctest::Approx(7680.0));  // session is just this round
}

TEST_CASE("faults map to OpenEVSE state codes") {
  FakeEvseState st; st.set_vehicle(true);
  st.fault = "gfci";     CHECK(st.state() == 6);
  st.fault = "noground"; CHECK(st.state() == 7);
  st.fault = "stuck";    CHECK(st.state() == 8);
  st.fault = "overtemp"; CHECK(st.state() == 10);
  st.fault = "none";     CHECK(st.state() == 3);
}
