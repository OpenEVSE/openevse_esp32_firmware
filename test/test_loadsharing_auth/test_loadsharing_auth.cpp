#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "loadsharing_auth.h"

static const std::string KEY = "0123456789abcdef-group-key";
static const std::string NONCE = "00112233445566778899aabbccddeeff";
static const std::string OTHER_NONCE = "ffeeddccbbaa99887766554433221100";
static const std::string BODY = "{\"loadsharing_role\":true}";

// Split out of the header so a test reads as the server's verification does.
static bool server_accepts(const std::string &header, const std::string &key,
                           const std::string &method, const std::string &path,
                           const std::string &body) {
  std::string nonce;
  std::string mac;
  return peer_auth_parse(header, nonce, mac) &&
         peer_auth_verify(key, method, path, nonce, body, mac);
}

TEST_CASE("a signed request verifies") {
  std::string header = peer_auth_header(KEY, "POST", "/config", NONCE, BODY);
  CHECK(server_accepts(header, KEY, "POST", "/config", BODY));
}

TEST_CASE("the signature matches an independent implementation") {
  // Python hmac/hashlib: tests/integration/test_loadsharing_peer_auth.py signs
  // the same way, so the two cannot drift without one of these failing.
  CHECK(peer_auth_header(KEY, "POST", "/config", NONCE, BODY) ==
        "OpenEVSE-Peer v1." + NONCE +
        ".38ac9c145de34e0616856170c66bb007826dafbba3b87ff12a8c372f3183eda6");
}

TEST_CASE("the key never appears in the header") {
  std::string header = peer_auth_header(KEY, "POST", "/config", NONCE, BODY);
  CHECK(header.find(KEY) == std::string::npos);
}

TEST_CASE("a signature only covers the request it was made for") {
  std::string header = peer_auth_header(KEY, "POST", "/config", NONCE, BODY);
  CHECK_FALSE(server_accepts(header, "another-group-key-value", "POST", "/config", BODY));
  CHECK_FALSE(server_accepts(header, KEY, "GET", "/config", BODY));
  CHECK_FALSE(server_accepts(header, KEY, "POST", "/loadsharing/peers", BODY));
  CHECK_FALSE(server_accepts(header, KEY, "POST", "/config", "{\"www_password\":\"x\"}"));
}

TEST_CASE("a tampered nonce or mac is rejected") {
  std::string header = peer_auth_header(KEY, "GET", "/status", NONCE, "");

  std::string badMac = header;
  badMac[badMac.size() - 1] = (badMac[badMac.size() - 1] == 'a') ? 'b' : 'a';
  CHECK_FALSE(server_accepts(badMac, KEY, "GET", "/status", ""));

  std::string badNonce = header;
  badNonce.replace(header.find(NONCE), NONCE.size(), OTHER_NONCE);
  CHECK_FALSE(server_accepts(badNonce, KEY, "GET", "/status", ""));
}

TEST_CASE("an empty key verifies nothing") {
  std::string header = peer_auth_header("", "GET", "/status", NONCE, "");
  CHECK_FALSE(server_accepts(header, "", "GET", "/status", ""));
}

TEST_CASE("only well-formed headers parse") {
  std::string header = peer_auth_header(KEY, "GET", "/status", NONCE, "");
  std::string nonce;
  std::string mac;

  CHECK(peer_auth_parse(header, nonce, mac));
  CHECK(nonce == NONCE);
  CHECK(mac.size() == 64);

  CHECK_FALSE(peer_auth_parse("", nonce, mac));
  CHECK_FALSE(peer_auth_parse("Basic dXNlcjpwYXNz", nonce, mac));
  CHECK_FALSE(peer_auth_parse(header + "0", nonce, mac));
  CHECK_FALSE(peer_auth_parse(header.substr(0, header.size() - 1), nonce, mac));

  std::string otherVersion = header;
  otherVersion.replace(otherVersion.find("v1."), 3, "v2.");
  CHECK_FALSE(peer_auth_parse(otherVersion, nonce, mac));

  std::string notHex = header;
  notHex[notHex.size() - 1] = 'Z';
  CHECK_FALSE(peer_auth_parse(notHex, nonce, mac));
}

TEST_CASE("the peer scheme is told apart from other credentials") {
  CHECK(peer_auth_is_peer_header(peer_auth_header(KEY, "GET", "/status", NONCE, "")));
  CHECK(peer_auth_is_peer_header("OpenEVSE-Peer garbage"));
  CHECK_FALSE(peer_auth_is_peer_header("Basic dXNlcjpwYXNz"));
  CHECK_FALSE(peer_auth_is_peer_header(""));
}

TEST_CASE("a peer credential reaches only the routes load sharing uses") {
  CHECK(peer_auth_scope_allows("GET", "/status"));
  CHECK(peer_auth_scope_allows("GET", "/config"));
  CHECK(peer_auth_scope_allows("GET", "/ws"));
  CHECK(peer_auth_scope_allows("POST", "/config"));
  CHECK(peer_auth_scope_allows("POST", "/loadsharing/peers"));

  CHECK_FALSE(peer_auth_scope_allows("POST", "/status"));
  CHECK_FALSE(peer_auth_scope_allows("POST", "/restart"));
  CHECK_FALSE(peer_auth_scope_allows("POST", "/update"));
  CHECK_FALSE(peer_auth_scope_allows("GET", "/debug/console"));
  CHECK_FALSE(peer_auth_scope_allows("GET", "/debug/crash/raw"));
  CHECK_FALSE(peer_auth_scope_allows("GET", "/loadsharing/peers"));
  CHECK_FALSE(peer_auth_scope_allows("DELETE", "/config"));
}

TEST_CASE("a peer may set only what the controller pushes") {
  CHECK(peer_auth_config_field_allowed("loadsharing_enabled"));
  CHECK(peer_auth_config_field_allowed("loadsharing_role"));
  CHECK(peer_auth_config_field_allowed("loadsharing_controller_host"));
  CHECK(peer_auth_config_field_allowed("loadsharing_group_id"));
  CHECK(peer_auth_config_field_allowed("loadsharing_safety_factor"));
  CHECK(peer_auth_config_field_allowed("loadsharing_heartbeat_timeout"));
  CHECK(peer_auth_config_field_allowed("loadsharing_failsafe_mode"));
  CHECK(peer_auth_config_field_allowed("loadsharing_failsafe_safe_current"));
  CHECK(peer_auth_config_field_allowed("loadsharing_failsafe_peer_assumed_current"));

  CHECK_FALSE(peer_auth_config_field_allowed("loadsharing_group_key"));
  CHECK_FALSE(peer_auth_config_field_allowed("loadsharing_group_max_current"));
  CHECK_FALSE(peer_auth_config_field_allowed("www_password"));
  CHECK_FALSE(peer_auth_config_field_allowed("www_username"));
  CHECK_FALSE(peer_auth_config_field_allowed("ssid"));
  CHECK_FALSE(peer_auth_config_field_allowed(""));
}

TEST_CASE("the group key is empty or long enough to resist guessing") {
  CHECK(peer_auth_key_valid(""));
  CHECK(peer_auth_key_valid(std::string(LOADSHARING_KEY_MIN_LEN, 'x')));
  CHECK(peer_auth_key_valid(std::string(LOADSHARING_KEY_MAX_LEN, 'x')));
  CHECK_FALSE(peer_auth_key_valid(std::string(LOADSHARING_KEY_MIN_LEN - 1, 'x')));
  CHECK_FALSE(peer_auth_key_valid(std::string(LOADSHARING_KEY_MAX_LEN + 1, 'x')));
}

TEST_CASE("a nonce works exactly once") {
  PeerNonceStore store;
  store.add(NONCE, 1000);
  CHECK(store.consume(NONCE, 1001));
  CHECK_FALSE(store.consume(NONCE, 1002));
}

TEST_CASE("a nonce that was never issued is rejected") {
  PeerNonceStore store;
  CHECK_FALSE(store.consume(NONCE, 1000));
  CHECK_FALSE(store.consume("", 1000));
  store.add(NONCE, 1000);
  CHECK_FALSE(store.consume(OTHER_NONCE, 1001));
  CHECK(store.consume(NONCE, 1002));
}

TEST_CASE("a nonce expires") {
  PeerNonceStore store;
  store.add(NONCE, 1000);
  CHECK(store.consume(NONCE, 1000 + LOADSHARING_NONCE_TTL_MS - 1));

  store.add(NONCE, 1000);
  CHECK_FALSE(store.consume(NONCE, 1000 + LOADSHARING_NONCE_TTL_MS));
}

TEST_CASE("nonce age survives the millis() rollover") {
  PeerNonceStore store;
  store.add(NONCE, 0xFFFFFF00UL);
  CHECK(store.consume(NONCE, 0x00000100UL));
}

TEST_CASE("a flood of nonces evicts the oldest and stays bounded") {
  PeerNonceStore store;
  store.add(NONCE, 1000);
  for(int i = 0; i < LOADSHARING_NONCE_SLOTS - 1; i++) {
    store.add("filler" + std::to_string(i), 1000);
  }
  CHECK(store.consume(NONCE, 1001));

  store.add(NONCE, 1000);
  for(int i = 0; i < LOADSHARING_NONCE_SLOTS; i++) {
    store.add("filler" + std::to_string(i), 1000);
  }
  CHECK_FALSE(store.consume(NONCE, 1001));
}
