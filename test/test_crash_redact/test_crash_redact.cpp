// Host-side tests for the crash-report config redaction (crash_redact.cpp).
// Spec §5: an allowlist of key names, never a blocklist -- a blocklist starts
// leaking the day someone adds a credential-bearing option and forgets it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <ArduinoJson.h>
#include <string>

#include "crash_redact.h"

TEST_CASE("every credential-bearing key in app_config is dropped") {
  // These follow from the unknown-key case below rather than adding coverage
  // to it -- the function says no to every string not in ALLOWED. They are a
  // regression list: if someone ever "helpfully" widens the allowlist, this is
  // the test that says which names must never be on it. (hideSecrets=true
  // already masks most of them in config_serialize, but ocpp_authkey is a
  // plain ConfigOptDefinition<String> and is NOT masked -- the allowlist is
  // the only thing keeping it out.)
  const char *secrets[] = {
    "ap_pass", "pass", "www_password", "www_username", "server_secret",
    "mqtt_pass", "mqtt_user", "emoncms_apikey", "ocpp_authkey",
    "tesla_access_token", "tesla_refresh_token",
  };
  for(const char *k : secrets) {
    CHECK_FALSE(crash_redact_key_allowed(k));
  }
}

TEST_CASE("an option nobody has written yet is dropped by default") {
  // The property that makes this an allowlist: the answer for an unknown key
  // is no. If this ever becomes yes, the next credential option leaks.
  CHECK_FALSE(crash_redact_key_allowed("future_cloud_api_token"));
  CHECK_FALSE(crash_redact_key_allowed(""));
  CHECK_FALSE(crash_redact_key_allowed(NULL));
}

TEST_CASE("the feature flags that explain a crash are kept") {
  CHECK(crash_redact_key_allowed("mqtt_enabled"));
  CHECK(crash_redact_key_allowed("ocpp_enabled"));
  CHECK(crash_redact_key_allowed("divert_enabled"));
  CHECK(crash_redact_key_allowed("rfid_enabled"));
  CHECK(crash_redact_key_allowed("tesla_enabled"));
  CHECK(crash_redact_key_allowed("sntp_enabled"));
  CHECK(crash_redact_key_allowed("emoncms_enabled"));
  CHECK(crash_redact_key_allowed("loadsharing_enabled"));
  CHECK(crash_redact_key_allowed("current_shaper_enabled"));
  CHECK(crash_redact_key_allowed("temp_throttle_enabled"));
  CHECK(crash_redact_key_allowed("buildenv"));
  CHECK(crash_redact_key_allowed("version"));
}

TEST_CASE("copying a whole config carries the flags and nothing else") {
  StaticJsonDocument<512> full;
  full["mqtt_enabled"] = true;
  full["mqtt_pass"] = "hunter2";
  full["mqtt_server"] = "mqtt.example";   // not a secret, but not on the list
  full["version"] = "4.2.0";

  StaticJsonDocument<512> out;
  JsonObject o = out.to<JsonObject>();
  crash_redact_config(full.as<JsonObjectConst>(), o);

  CHECK(o["mqtt_enabled"].as<bool>() == true);
  CHECK(o["version"].as<const char *>() == std::string("4.2.0"));
  CHECK_FALSE(o.containsKey("mqtt_pass"));
  CHECK_FALSE(o.containsKey("mqtt_server"));
}
