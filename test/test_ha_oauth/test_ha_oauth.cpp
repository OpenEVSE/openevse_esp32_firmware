#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ha_oauth.h"

TEST_CASE("ha_url_encode percent-encodes reserved characters") {
  CHECK(ha_url_encode("abc-123_.~") == "abc-123_.~");
  CHECK(ha_url_encode("http://h:8123/") == "http%3A%2F%2Fh%3A8123%2F");
  CHECK(ha_url_encode("a b") == "a%20b");
  CHECK(ha_url_encode("\xFF") == "%FF");                 // high byte always encoded
  CHECK(ha_url_encode("a=1&b=2+3") == "a%3D1%26b%3D2%2B3"); // OAuth param chars
}

TEST_CASE("ha_derive_base_url joins scheme and host") {
  CHECK(ha_derive_base_url("http", "openevse.local") == "http://openevse.local");
  CHECK(ha_derive_base_url("https", "10.0.0.5:443") == "https://10.0.0.5:443");
  CHECK(ha_derive_base_url("", "h") == "http://h");          // default scheme
  CHECK(ha_derive_base_url("http", "h/") == "http://h");     // trailing slash stripped
}

TEST_CASE("ha_build_redirect_uri appends the callback path") {
  CHECK(ha_build_redirect_uri("http://openevse.local") ==
        "http://openevse.local/ha_callback");
}

TEST_CASE("ha_build_authorize_url builds an encoded query") {
  std::string url = ha_build_authorize_url(
      "http://homeassistant.local:8123",
      "http://openevse.local/",
      "http://openevse.local/ha_callback",
      "abc123");
  CHECK(url ==
        "http://homeassistant.local:8123/auth/authorize"
        "?response_type=code"
        "&client_id=http%3A%2F%2Fopenevse.local%2F"
        "&redirect_uri=http%3A%2F%2Fopenevse.local%2Fha_callback"
        "&state=abc123");
}

TEST_CASE("ha_build_authorize_url strips a trailing slash from ha_url") {
  std::string url = ha_build_authorize_url(
      "http://ha:8123/", "cid", "ruri", "s");
  CHECK(url.rfind("http://ha:8123/auth/authorize", 0) == 0);
}

TEST_CASE("ha_build_token_exchange_body encodes the form body") {
  CHECK(ha_build_token_exchange_body("http://openevse.local/", "the+code") ==
        "grant_type=authorization_code"
        "&code=the%2Bcode"
        "&client_id=http%3A%2F%2Fopenevse.local%2F");
}

TEST_CASE("ha_build_refresh_body encodes the form body") {
  CHECK(ha_build_refresh_body("cid", "rtok") ==
        "grant_type=refresh_token"
        "&refresh_token=rtok"
        "&client_id=cid");
}

TEST_CASE("ha_parse_token_response extracts tokens and expiry") {
  HaTokens t;
  bool ok = ha_parse_token_response(
      "{\"access_token\":\"AT\",\"refresh_token\":\"RT\","
      "\"expires_in\":1800,\"token_type\":\"Bearer\"}", t);
  CHECK(ok);
  CHECK(t.access_token == "AT");
  CHECK(t.refresh_token == "RT");
  CHECK(t.expires_in == 1800);
}

TEST_CASE("ha_parse_token_response: refresh response without refresh_token is ok") {
  HaTokens t;
  bool ok = ha_parse_token_response(
      "{\"access_token\":\"AT2\",\"expires_in\":1800}", t);
  CHECK(ok);
  CHECK(t.access_token == "AT2");
  CHECK(t.refresh_token.empty());
}

TEST_CASE("ha_parse_token_response fails on error / malformed bodies") {
  HaTokens t;
  CHECK_FALSE(ha_parse_token_response("{\"error\":\"invalid_grant\"}", t));
  CHECK_FALSE(ha_parse_token_response("not json", t));
  CHECK_FALSE(ha_parse_token_response("{\"expires_in\":1800}", t)); // no access_token
}

TEST_CASE("ha_compute_expiry adds expires_in to now") {
  CHECK(ha_compute_expiry(1000, 1800) == 2800);
  CHECK(ha_compute_expiry(1000, 0) == 0);    // unknown expiry
  CHECK(ha_compute_expiry(1000, -5) == 0);
}

TEST_CASE("ha_refresh_due triggers within the margin") {
  // expiry at 2800, margin 300 -> refresh due at >= 2500
  CHECK_FALSE(ha_refresh_due(2800, 2499, 300));
  CHECK(ha_refresh_due(2800, 2500, 300));
  CHECK(ha_refresh_due(2800, 3000, 300));    // already expired
  CHECK(ha_refresh_due(0, 5, 300));          // unknown expiry -> always due
}

TEST_CASE("ha_parse_entity_state extracts the state, ignoring attributes") {
  std::string s;
  CHECK(ha_parse_entity_state(
      "{\"entity_id\":\"sensor.car\",\"state\":\"73.5\","
      "\"attributes\":{\"unit_of_measurement\":\"%\",\"friendly_name\":\"Car\"}}", s));
  CHECK(s == "73.5");

  // Large attributes blob: would overflow the result doc if not filtered out.
  std::string big = "{\"state\":\"42\",\"attributes\":{\"blob\":\"";
  big += std::string(400, 'x');
  big += "\"}}";
  std::string s2;
  CHECK(ha_parse_entity_state(big, s2));
  CHECK(s2 == "42");
}

TEST_CASE("ha_parse_entity_state rejects unavailable/unknown/missing/bad") {
  std::string s;
  CHECK_FALSE(ha_parse_entity_state("{\"state\":\"unavailable\"}", s));
  CHECK_FALSE(ha_parse_entity_state("{\"state\":\"unknown\"}", s));
  CHECK_FALSE(ha_parse_entity_state("{\"state\":\"\"}", s));
  CHECK_FALSE(ha_parse_entity_state("{\"attributes\":{}}", s)); // no state key
  CHECK_FALSE(ha_parse_entity_state("not json", s));
}
