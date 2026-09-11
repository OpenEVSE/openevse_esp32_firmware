#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <Arduino.h>
#include <map>
#include "web_server_tls_startup.h"

// Compile the real group implementation with only its hardware collaborators
// replaced. In particular, exercise getAllPeers(), not a copy of its logic.
#define _EMONESP_WIFI_H
static struct {
  String ip;
  String getIp() { return ip; }
} net;

#define _EMONESP_WEB_SERVER_H
static WebServerListenerState listener;
bool web_server_is_running() { return listener.started; }
bool web_server_is_https() { return listener.started && listener.https; }
uint16_t web_server_port() { return listener.port; }

#define LOADSHARING_DISCOVERY_TASK_H
struct DiscoveredPeer {
  String hostname, ipAddress, url, id, name;
  uint16_t port;
  std::map<String, String> txtRecords;
};

#include "../../src/loadsharing_types.cpp"

String esp_hostname = "dummy-evse";
bool loadsharing_enabled = false;
String loadsharing_role;
String loadsharing_controller_host;
uint32_t loadsharing_heartbeat_timeout = 10;
uint32_t loadsharing_peers_version = 0;

TEST_CASE("local peer refresh preserves failed listener state despite an IP") {
  listener = web_server_start_listeners(
    "certificate", "key", 443, 80,
    [](const char *, const char *) { return false; },
    []() { return false; });
  net.ip = "127.0.0.1";

  LoadSharingGroupState group;
  group.loadGroupPeers();
  REQUIRE(group.getLocalPeer() != nullptr);
  REQUIRE_FALSE(group.getLocalPeer()->isOnline());

  for (int refresh = 0; refresh < 2; ++refresh) {
    auto peers = group.getAllPeers(true, true);
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].isLocal);
    CHECK(peers[0].ipAddress == "127.0.0.1");
    CHECK(peers[0].url.isEmpty());
    CHECK_FALSE(peers[0].online);
    CHECK_FALSE(group.getLocalPeer()->isOnline());
  }
}

TEST_CASE("local peer refresh reports the running fallback and preserves remote state") {
  listener = web_server_start_listeners(
    "certificate", "key", 443, 8000,
    [](const char *, const char *) { return false; },
    []() { return true; });
  net.ip = "";

  LoadSharingGroupState group;
  group.loadGroupPeers();
  LoadSharingPeer remote("remote.example.invalid");
  remote.setOnline(false);
  group.getPeers().push_back(remote);

  // The listener is authoritative even before the network task reports an IP.
  auto peers = group.getAllPeers(true, true);
  REQUIRE(peers.size() == 2);
  CHECK(peers[0].online);
  CHECK(peers[0].url == "http://dummy-evse.local:8000");
  CHECK_FALSE(peers[1].online);

  net.ip = "127.0.0.1";
  peers = group.getAllPeers(true, true);
  CHECK(peers[0].online);
  CHECK(peers[0].ipAddress == "127.0.0.1");
  CHECK_FALSE(peers[1].online);
}
