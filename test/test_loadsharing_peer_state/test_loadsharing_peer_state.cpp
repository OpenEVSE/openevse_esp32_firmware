#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <Arduino.h>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <LittleFS.h>
#include "web_server_tls_startup.h"

// Compile the real group implementation with its network/identity collaborators
// replaced and the shared native filesystem mounted in an isolated directory.
// In particular, exercise getAllPeers(), not a copy of its logic.
#define _EMONESP_WIFI_H
static struct {
  String ip;
  /** Return this test's independently controlled network address. */
  String getIp() { return ip; }
} net;

#define _EMONESP_WEB_SERVER_H
static WebServerListenerState listener;
/** Expose the selected test listener's availability to the real peer code. */
bool web_server_is_running() { return listener.started; }
/** Expose TLS only when the selected test listener actually started. */
bool web_server_is_https() { return listener.started && listener.https; }
/** Expose the selected test listener's port to the real peer code. */
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
bool loadsharing_role = false;
String loadsharing_controller_host;
uint32_t loadsharing_heartbeat_timeout = 10;
uint32_t loadsharing_peers_version = 0;

/** Mount an empty peer store without sharing files or environment state between tests. */
struct EmptyPeerFilesystem {
  std::string directory;
  std::string previous_root;
  bool had_root;

  /** Mount the shared native backend at a fresh private temporary directory. */
  EmptyPeerFilesystem() {
    const char *root = std::getenv("EPOXY_FS_ROOT");
    had_root = root != nullptr;
    if(had_root) {
      previous_root = root;
    }
    directory = (std::filesystem::temp_directory_path() / "openevse-peer-state-XXXXXX").string();
    REQUIRE(mkdtemp(&directory[0]) != nullptr);
    REQUIRE(setenv("EPOXY_FS_ROOT", directory.c_str(), 1) == 0);
    REQUIRE(LittleFS.begin());
  }

  /** Unmount, reject unexpected persisted data, and restore the caller's root override. */
  ~EmptyPeerFilesystem() {
    LittleFS.end();
    std::error_code error;
    CHECK(std::filesystem::remove(directory, error));
    CHECK_FALSE(error);
    if(had_root) {
      CHECK(setenv("EPOXY_FS_ROOT", previous_root.c_str(), 1) == 0);
    } else {
      CHECK(unsetenv("EPOXY_FS_ROOT") == 0);
    }
  }
};

/** Repeated peer refresh must not turn an IP address into listener availability. */
TEST_CASE("local peer refresh preserves failed listener state despite an IP") {
  EmptyPeerFilesystem filesystem;
  listener = web_server_start_listeners(
    "certificate", "key", 443, 80,
    /** Model failed TLS startup for the local peer. */
    [](const char *, const char *) { return false; },
    /** Model failed HTTP fallback for the local peer. */
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

/** HTTP fallback remains available across IP refresh without changing remote peers. */
TEST_CASE("local peer refresh reports the running fallback and preserves remote state") {
  EmptyPeerFilesystem filesystem;
  listener = web_server_start_listeners(
    "certificate", "key", 443, 8000,
    /** Reject TLS so the local peer must describe HTTP instead. */
    [](const char *, const char *) { return false; },
    /** Select the working HTTP fallback for the local peer. */
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
