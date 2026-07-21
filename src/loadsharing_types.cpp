/*
 * MIT License
 * Copyright (c) 2025 Jeremy Poulter
 *
 * LoadSharingGroupState - Group peer management and persistence
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LOADSHARING_DISCOVERY)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "loadsharing_types.h"
#include "loadsharing_discovery_task.h"
#include "app_config.h"
#include "net_manager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <espal.h>

// Global instance
LoadSharingGroupState loadSharingGroupState;

bool LoadSharingGroupState::isController() const {
  return loadsharing_enabled && loadsharing_role == "controller";
}

bool LoadSharingGroupState::isMember() const {
  return loadsharing_enabled && loadsharing_role == "member";
}

void LoadSharingGroupState::becomeMember(const String& controllerHost) {
  loadsharing_enabled = true;
  loadsharing_role = "member";
  loadsharing_controller_host = controllerHost;
}

void LoadSharingGroupState::resetRole() {
  loadsharing_role = "";
  loadsharing_controller_host = "";
  _failsafe_active = false;
}

void LoadSharingGroupState::checkMemberFailsafe() {
  if (!isMember()) {
    _failsafe_active = false;
    return;
  }

  if (loadsharing_controller_host.length() == 0) {
    _failsafe_active = true;
    return;
  }

  unsigned long timeoutMs = loadsharing_heartbeat_timeout * 1000UL;
  if (timeoutMs == 0) {
    timeoutMs = 10000;
  }

  if (_last_allocation_received_ms == 0) {
    _failsafe_active = true;
    return;
  }

  _failsafe_active = ((long)(millis() - (_last_allocation_received_ms + timeoutMs)) >= 0);
}

void LoadSharingGroupState::notifyPeerChange() {
  loadsharing_peers_version++;
  if (_onPeerChange) {
    _onPeerChange();
  }
}

void LoadSharingGroupState::onDiscoveryComplete(
    const std::vector<DiscoveredPeer>& discoveredPeers) {

  bool changed = false;
  // Tracks changes to persisted fields (id/host) of joined members, so we only
  // rewrite flash when the saved representation actually changes -- not on
  // every discovery sweep that merely toggles a non-member's online flag.
  bool persistChanged = false;

  // Update existing peers with discovery info (IP, online status, URL).
  // Match by stable device id first (a peer added by hostname reaches the same
  // device the discovery advertises under its mDNS hostname), falling back to
  // hostname when the id is not yet known. Matching by id prevents the same
  // device appearing as two rows (e.g. joined "localhost:8001" and discovered
  // "openevse-ev1.local").
  for (auto& peer : _peers) {
    bool found = false;
    for (const auto& discovered : discoveredPeers) {
      bool idMatch = !peer.getId().isEmpty() && !discovered.id.isEmpty() &&
                     peer.getId() == discovered.id;
      bool hostMatch = discovered.hostname == peer.getHost();
      if (idMatch || hostMatch) {
        if (!peer.isOnline() || peer.getIp() != discovered.ipAddress) {
          changed = true;
        }
        if (!discovered.ipAddress.isEmpty() && discovered.ipAddress != "0.0.0.0") {
          peer.setIp(discovered.ipAddress);
        }
        peer.setOnline(true);
        // Adopt the discovered mDNS hostname as the peer's host so it displays
        // (and is reached) by its friendly name (e.g. "openevse-ev1.local")
        // rather than however it was originally added (e.g. "localhost:8001").
        // Append the port only when it is non-default for the scheme, matching
        // the convention used elsewhere.
        if (!discovered.hostname.isEmpty()) {
          bool ssl = false;
          auto sslIt = discovered.txtRecords.find("ssl");
          if (sslIt != discovered.txtRecords.end() && sslIt->second == "1") {
            ssl = true;
          }
          String newHost = discovered.hostname;
          if (discovered.port > 0 &&
              ((ssl && discovered.port != 443) || (!ssl && discovered.port != 80))) {
            newHost += ":" + String(discovered.port);
          }
          if (peer.getHost() != newHost) {
            peer.setHost(newHost);
            changed = true;
            if (peer.isJoined()) persistChanged = true;
          }
        }
        if (!discovered.url.isEmpty()) {
          peer.setUrl(discovered.url);
        }
        if (!discovered.name.isEmpty()) {
          peer.setName(discovered.name);
        }
        if (!discovered.id.isEmpty() && peer.getId() != discovered.id) {
          peer.setId(discovered.id);
          if (peer.isJoined()) persistChanged = true;
        }
        found = true;
        break;
      }
    }
    if (!found && peer.isOnline()) {
      peer.setOnline(false);
      changed = true;
    }
  }

  // Add newly-discovered peers not already in _peers (joined=false). Skip any
  // discovery that matches an existing peer by hostname or by device id so a
  // group member added by hostname is not duplicated once discovered.
  for (const auto& discovered : discoveredPeers) {
    if (isLocalHost(discovered.hostname)) continue;
    if (getPeerByHost(discovered.hostname)) continue;
    if (!discovered.id.isEmpty() && getPeerById(discovered.id)) continue;

    LoadSharingPeer peer(discovered.hostname);
    peer.setIp(discovered.ipAddress);
    peer.setOnline(true);
    peer.setUrl(discovered.url);
    peer.setName(discovered.name);
    peer.setId(discovered.id);
    peer.setJoined(false);
    _peers.push_back(peer);
    changed = true;
  }

  if (persistChanged) {
    // A joined member's persisted identity (id/host) changed; rewrite the
    // saved list so the reconciliation survives a restart.
    saveGroupPeers();
  }

  if (changed) {
    DBUGF("LoadSharingGroupState: Discovery update changed peer status");
    notifyPeerChange();
  }
}

bool LoadSharingGroupState::addGroupPeer(const String& hostname) {
  if (isLocalHost(hostname)) {
    DBUGF("LoadSharingGroupState: Cannot add local device as peer: %s", hostname.c_str());
    return false;
  }

  // Find existing peer or create new one
  LoadSharingPeer* existing = getPeerByHost(hostname);
  if (existing) {
    if (existing->isJoined()) {
      DBUGF("LoadSharingGroupState: Peer already in group: %s", hostname.c_str());
      return false;
    }
    existing->setJoined(true);
  } else {
    LoadSharingPeer newPeer(hostname);
    newPeer.setJoined(true);
    _peers.push_back(newPeer);
  }

  saveGroupPeers();

  DBUGF("LoadSharingGroupState: Added peer to group: %s (total: %u)",
        hostname.c_str(), (unsigned int)_peers.size());

  notifyPeerChange();
  return true;
}

bool LoadSharingGroupState::removeGroupPeer(const String& hostname) {
  if (isLocalHost(hostname)) {
    DBUGF("LoadSharingGroupState: Cannot remove local device from group: %s", hostname.c_str());
    return false;
  }

  LoadSharingPeer* peer = getPeerByHost(hostname);
  if (!peer || !peer->isJoined()) {
    DBUGF("LoadSharingGroupState: Peer not found: %s", hostname.c_str());
    return false;
  }

  peer->setJoined(false);
  saveGroupPeers();

  DBUGF("LoadSharingGroupState: Removed peer from group: %s", hostname.c_str());

  notifyPeerChange();
  return true;
}

bool LoadSharingGroupState::isLocalHost(const String& hostname) const {
  String localMdns = esp_hostname + String(".local");
  if (hostname.equalsIgnoreCase(esp_hostname) ||
      hostname.equalsIgnoreCase(localMdns)) {
    return true;
  }
  if (hostname == ESPAL.getLongId()) {
    return true;
  }
  return false;
}

String LoadSharingGroupState::getLocalHostname() const {
  return esp_hostname + String(".local");
}

std::vector<LoadSharingGroupState::PeerInfo> LoadSharingGroupState::getAllPeers(
    bool includeDiscovered, bool includeGroup) const {

  std::vector<PeerInfo> result;
  String localHostname = getLocalHostname();

  for (auto& peer : _peers) {
    bool isLocal = (peer.getHost() == localHostname);

    // Refresh local peer with live network state (IP may have been empty at boot)
    if (isLocal) {
      const_cast<LoadSharingPeer&>(peer).setIp(net.getIp());
      const_cast<LoadSharingPeer&>(peer).setOnline(net.getIp().length() > 0);
    }

    // Skip non-local online-only peers when includeDiscovered is false
    if (!includeDiscovered && peer.isOnline() && !isLocal) continue;
    // Skip non-local joined peers when includeGroup is false
    if (!includeGroup && peer.isJoined() && !isLocal) continue;

    PeerInfo info;
    info.hostname = peer.getHost();
    info.ipAddress = peer.getIp();
    info.id = peer.getId();
    info.name = peer.getName();
    info.url = peer.getUrl();
    info.online = peer.isOnline();
    info.joined = peer.isJoined();
    info.isLocal = isLocal;
    result.push_back(info);
  }

  return result;
}

void LoadSharingGroupState::addLocalPeer() {
  String localHostname = getLocalHostname();

  // Build local peer entry
  LoadSharingPeer local(localHostname);
  local.setId(ESPAL.getLongId());
  local.setName(String(esp_hostname));
  local.setIp(net.getIp());
  bool ssl = config_https_enabled();
  uint16_t port = ssl ? www_https_port : www_http_port;
  String localUrl = ssl ? "https://" : "http://";
  localUrl += localHostname;
  if ((ssl && port != 443) || (!ssl && port != 80)) {
    localUrl += ":" + String(port);
  }
  local.setUrl(localUrl);
  local.setPort(port);
  local.setOnline(true);
  local.setJoined(true);

  // Insert at front so it's always first
  _peers.insert(_peers.begin(), local);
}

bool LoadSharingGroupState::loadGroupPeers() {
  const char* filePath = "/loadsharing_peers.json";

  _peers.clear();

  // Always add the local peer first
  addLocalPeer();

  if (!LittleFS.exists(filePath)) {
    DBUGLN("LoadSharingGroupState: No persisted group peer list found");
    return false;
  }

  File file = LittleFS.open(filePath, "r");
  if (!file) {
    DBUGF("LoadSharingGroupState: Failed to open group peer list file: %s", filePath);
    return false;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    DBUGF("LoadSharingGroupState: Failed to parse group peer list JSON: %s", error.c_str());
    return false;
  }

  JsonArray peers = doc["peers"].as<JsonArray>();
  for (JsonVariant peer : peers) {
    // Support both the current object form ({id, host}) and the legacy form
    // where each entry was a bare host string.
    String hostname;
    String id;
    if (peer.is<JsonObject>()) {
      hostname = peer["host"].as<String>();
      id = peer["id"].as<String>();
    } else {
      hostname = peer.as<String>();
    }
    if (hostname.isEmpty() && id.isEmpty()) continue;
    if (isLocalHost(hostname)) continue;  // Skip local host in saved list

    // Re-match an already-present peer by id first (survives host changes),
    // then by host.
    LoadSharingPeer* existing = nullptr;
    if (!id.isEmpty()) {
      existing = getPeerById(id);
    }
    if (existing == nullptr && !hostname.isEmpty()) {
      existing = getPeerByHost(hostname);
    }
    if (existing) {
      existing->setJoined(true);
      if (existing->getId().isEmpty() && !id.isEmpty()) {
        existing->setId(id);
      }
    } else {
      LoadSharingPeer p(hostname);
      p.setId(id);
      p.setJoined(true);
      _peers.push_back(p);
    }
  }

  DBUGF("LoadSharingGroupState: Loaded %u group peers", (unsigned int)peers.size());
  return true;
}

bool LoadSharingGroupState::saveGroupPeers() {
  const char* filePath = "/loadsharing_peers.json";
  const char* tempPath = "/loadsharing_peers.json.tmp";

  File file = LittleFS.open(tempPath, "w");
  if (!file) {
    DBUGF("LoadSharingGroupState: Failed to open temp file for writing: %s", tempPath);
    return false;
  }

  DynamicJsonDocument doc(1024);
  JsonArray peers = doc.createNestedArray("peers");
  for (const auto& peer : _peers) {
    if (peer.isJoined() && !isLocalHost(peer.getHost())) {
      // Persist both the stable device id and the host so a peer can be
      // re-matched by id after a restart (discovery may re-key it under a
      // different reachable host). Legacy entries stored a bare host string.
      JsonObject obj = peers.createNestedObject();
      obj["id"] = peer.getId();
      obj["host"] = peer.getHost();
    }
  }

  if (serializeJson(doc, file) == 0) {
    file.close();
    DBUGLN("LoadSharingGroupState: Failed to write group peer list JSON");
    return false;
  }

  file.close();

  if (LittleFS.exists(filePath)) {
    LittleFS.remove(filePath);
  }
  if (!LittleFS.rename(tempPath, filePath)) {
    DBUGF("LoadSharingGroupState: Failed to rename temp file to %s", filePath);
    return false;
  }

  DBUGF("LoadSharingGroupState: Saved group peers");
  return true;
}
