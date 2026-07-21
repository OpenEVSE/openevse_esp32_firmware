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

  // Update existing peers with discovery info (IP, online status, URL)
  for (auto& peer : _peers) {
    bool found = false;
    for (const auto& discovered : discoveredPeers) {
      if (discovered.hostname == peer.getHost()) {
        if (!peer.isOnline() || peer.getIp() != discovered.ipAddress) {
          changed = true;
        }
        if (!discovered.ipAddress.isEmpty() && discovered.ipAddress != "0.0.0.0") {
          peer.setIp(discovered.ipAddress);
        }
        peer.setOnline(true);
        if (!discovered.url.isEmpty()) {
          peer.setUrl(discovered.url);
        }
        if (!discovered.name.isEmpty()) {
          peer.setName(discovered.name);
        }
        if (!discovered.id.isEmpty()) {
          peer.setId(discovered.id);
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

  // Add newly-discovered peers not already in _peers (joined=false)
  for (const auto& discovered : discoveredPeers) {
    if (isLocalHost(discovered.hostname)) continue;
    if (getPeerByHost(discovered.hostname)) continue;

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
    String hostname = peer.as<String>();
    if (isLocalHost(hostname)) continue;  // Skip local host in saved list

    LoadSharingPeer* existing = getPeerByHost(hostname);
    if (existing) {
      existing->setJoined(true);
    } else {
      LoadSharingPeer p(hostname);
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
      peers.add(peer.getHost());
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
