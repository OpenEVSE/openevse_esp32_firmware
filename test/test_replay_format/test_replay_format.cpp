// Host-side tests for the replay package building blocks: the sample ring,
// sample encoding, claim diffing (replay_format.*, replay_ring.h) and the
// configuration allowlist (replay_redact.cpp).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <ArduinoJson.h>
#include <string>
#include <vector>

#include "replay_format.h"
#include "replay_redact.h"
#include "replay_ring.h"

TEST_CASE("ring keeps the newest entries, oldest first") {
  ReplayRing<int> ring;
  CHECK_FALSE(ring.ready());
  ring.push(1);  // before begin(): ignored, no crash
  REQUIRE(ring.begin(3));
  for(int i = 1; i <= 5; i++) {
    ring.push(i);
  }
  REQUIRE(ring.size() == 3);
  CHECK(ring.at(0) == 3);
  CHECK(ring.at(1) == 4);
  CHECK(ring.at(2) == 5);
  CHECK(*ring.newest() == 5);
  CHECK(ring.overwritten() == 2);
  ring.clear();
  CHECK(ring.size() == 0);
  CHECK(ring.newest() == nullptr);
}

TEST_CASE("sample encoding round-trips engineering units") {
  ReplaySample s;
  replay_sample_set(s, 1234, 4321.0, -1500.0, 6200.0, 241.3, 15.98, true, 41.25,
                    5432.0, 16, 3, 80, 16, REPLAY_FLAG_VEHICLE | REPLAY_FLAG_CHARGING);
  char buf[160];
  REQUIRE(replay_sample_row(s, buf, sizeof(buf)) > 0);
  CHECK(std::string(buf) == "[1234,4320,-1500,6200,241.3,15.98,41.3,5430,16,3,80,16,3]");
}

TEST_CASE("unknown temperature and SoC are written as null") {
  ReplaySample s;
  replay_sample_set(s, 0, 0, 0, 0, 0, 0, false, 99, 0, 0, 1, -1, 0, 0);
  char buf[160];
  REQUIRE(replay_sample_row(s, buf, sizeof(buf)) > 0);
  CHECK(std::string(buf) == "[0,0,0,0,0.0,0.00,null,0,0,1,null,0,0]");
}

TEST_CASE("sample encoding clamps out-of-range values") {
  ReplaySample s;
  replay_sample_set(s, 0, 1e9, -1e9, 0, -5, 1e6, true, 0, 1e9, 400, 1, 101, 1000, 0);
  CHECK(s.solar_dw == INT16_MAX);
  CHECK(s.grid_ie_dw == INT16_MIN);
  CHECK(s.volts_dv == 0);
  CHECK(s.amps_ca == UINT16_MAX);
  CHECK(s.session_dwh == UINT16_MAX);
  CHECK(s.pilot == 255);
  CHECK(s.soc == REPLAY_SOC_INVALID);
  CHECK(s.target_current == 255);
}

TEST_CASE("too small a buffer writes nothing") {
  ReplaySample s;
  replay_sample_set(s, 1, 0, 0, 0, 240, 0, true, 20, 0, 32, 1, 50, 32, 0);
  char buf[8];
  CHECK(replay_sample_row(s, buf, sizeof(buf)) == 0);
}

TEST_CASE("column names match the row width") {
  ReplaySample s;
  replay_sample_set(s, 1, 0, 0, 0, 240, 0, true, 20, 0, 32, 1, 50, 32, 0);
  char buf[160];
  replay_sample_row(s, buf, sizeof(buf));
  size_t commas = 0;
  for(char *p = buf; *p; p++) commas += (',' == *p);
  CHECK(commas + 1 == REPLAY_SAMPLE_COLUMN_COUNT);
}

static ReplayClaim claim(uint32_t client, int16_t prio, uint8_t state,
                         int16_t cc = -1, int16_t mc = -1)
{
  ReplayClaim c = {client, prio, cc, mc, state, 0};
  return c;
}

TEST_CASE("claim diff reports new, changed and dropped claims") {
  ReplayClaim prev[] = {
    claim(1, 1000, REPLAY_STATE_DISABLED),
    claim(2, 50, REPLAY_STATE_ACTIVE, 16),
    claim(3, 5000, REPLAY_STATE_NONE, -1, 20),
  };
  ReplayClaim cur[] = {
    claim(2, 50, REPLAY_STATE_ACTIVE, 12),       // changed current
    claim(3, 5000, REPLAY_STATE_NONE, -1, 20),   // unchanged
    claim(4, 1050, REPLAY_STATE_ACTIVE),         // new
  };
  std::vector<uint32_t> claimed, released;
  size_t n = replay_diff_claims(prev, 3, cur, 3,
    [&](const ReplayClaim &c) { claimed.push_back(c.client); },
    [&](const ReplayClaim &c) { released.push_back(c.client); });
  CHECK(n == 3);
  CHECK(claimed == std::vector<uint32_t>{2, 4});
  CHECK(released == std::vector<uint32_t>{1});
}

TEST_CASE("identical claim sets produce no events") {
  ReplayClaim set[] = { claim(7, 100, REPLAY_STATE_ACTIVE, 32) };
  size_t n = replay_diff_claims(set, 1, set, 1,
    [](const ReplayClaim &) { FAIL("unexpected claim"); },
    [](const ReplayClaim &) { FAIL("unexpected release"); });
  CHECK(n == 0);
}

TEST_CASE("event and state names") {
  CHECK(std::string(replay_event_type_name((uint8_t)ReplayEventType::RfidDeauth)) == "rfid_deauth");
  CHECK(std::string(replay_event_type_name(200)) == "unknown");
  CHECK(std::string(replay_state_name(REPLAY_STATE_DISABLED)) == "disabled");
}

TEST_CASE("credentials and identifying settings never reach a replay package") {
  const char *never[] = {
    "ap_pass", "pass", "ssid", "ap_ssid", "www_password", "www_username",
    "server_secret", "mqtt_pass", "mqtt_user", "mqtt_server", "mqtt_topic",
    "mqtt_solar", "emoncms_apikey", "emoncms_server", "ocpp_authkey",
    "ocpp_server", "ocpp_idtag", "tesla_access_token", "tesla_refresh_token",
    "tesla_vehicle_id", "hostname", "sntp_hostname", "loadsharing_group_id",
    "loadsharing_controller_host", "rfid_storage", "", nullptr,
  };
  for(const char *k : never) {
    CHECK_FALSE(replay_redact_key_allowed(k));
  }
  CHECK(replay_redact_key_allowed("divert_enabled"));
  CHECK(replay_redact_key_allowed("time_zone"));
}

TEST_CASE("rfid tags are replaced by numbered placeholders") {
  StaticJsonDocument<512> full;
  full["rfid_enabled"] = true;
  full["rfid_storage"] = "04A1B2C3, 0499AA ,77FF";
  full["mqtt_pass"] = "hunter2";
  full["ocpp_authkey"] = "secret";
  StaticJsonDocument<512> out;
  replay_redact_config(full.as<JsonObjectConst>(), out.to<JsonObject>());
  CHECK(out["rfid_enabled"] == true);
  CHECK(std::string(out["rfid_storage"].as<const char *>()) == "REPLAY01,REPLAY02,REPLAY03");
  CHECK_FALSE(out.containsKey("mqtt_pass"));
  CHECK_FALSE(out.containsKey("ocpp_authkey"));
}

TEST_CASE("no stored tags, no rfid_storage") {
  StaticJsonDocument<256> full;
  full["rfid_storage"] = "";
  StaticJsonDocument<256> out;
  replay_redact_config(full.as<JsonObjectConst>(), out.to<JsonObject>());
  CHECK_FALSE(out.containsKey("rfid_storage"));
}
