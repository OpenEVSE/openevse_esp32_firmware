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
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <espal.h>

// Global instance
LoadSharingGroupState loadSharingGroupState;

void LoadSharingGroupState::notifyPeerChange() {
  if (_onPeerChange) {
    _onPeerChange();
  }
}

void LoadSharingGroupState::ensurePeerEntry(const String& hostname) {
  for (auto& peer : _peers) {
    if (peer.getHost() == hostname) {
      return;  // Already exists
    }
  }
  // Create a new offline peer entry
  LoadSharingPeer newPeer;
  newPeer.setHost(hostname);
  _peers.push_back(newPeer);
}

void LoadSharingGroupState::onDiscoveryComplete() {
  if (!_discoveredPeers) {
    return;
  }

  bool changed = false;

  // Update group peers with discovery info (IP address, online status)
  for (auto& peer : _peers) {
    bool found = false;
    for (const auto& discovered : *_discoveredPeers) {
      if (discovered.hostname == peer.getHost()) {
        // Peer was discovered - update IP, port and mark online
        if (!peer.isOnline() || peer.getIp() != discovered.ipAddress) {
          changed = true;
        }
        peer.setIp(discovered.ipAddress);
        peer.setPort(discovered.port);
        peer.setOnline(true);
        found = true;
        break;
      }
    }
    if (!found && peer.isOnline()) {
      // Peer was previously online but not discovered this time
      peer.setOnline(false);
      changed = true;
    }
  }

  if (changed) {
    DBUGF("LoadSharingGroupState: Discovery update changed peer status");
    notifyPeerChange();
  }
}

bool LoadSharingGroupState::addGroupPeer(const String& hostname) {
  // Block adding the local device as a remote peer
  if (isLocalHost(hostname)) {
    DBUGF("LoadSharingGroupState: Cannot add local device as peer: %s", hostname.c_str());
    return false;
  }

  // Check for duplicates
  for (const auto& peer : _groupPeers) {
    if (peer == hostname) {
      DBUGF("LoadSharingGroupState: Peer already in group: %s", hostname.c_str());
      return false;
    }
  }

  _groupPeers.push_back(hostname);
  _groupPeersDirty = true;
  saveGroupPeers();

  // Also add to the active peer list
  ensurePeerEntry(hostname);

  DBUGF("LoadSharingGroupState: Added peer to group: %s (total: %u)",
        hostname.c_str(), (unsigned int)_groupPeers.size());

  notifyPeerChange();
  return true;
}

bool LoadSharingGroupState::removeGroupPeer(const String& hostname) {
  // Block removal of the local device
  if (isLocalHost(hostname)) {
    DBUGF("LoadSharingGroupState: Cannot remove local device from group: %s", hostname.c_str());
    return false;
  }

  for (size_t i = 0; i < _groupPeers.size(); i++) {
    if (_groupPeers[i] == hostname) {
      _groupPeers.erase(_groupPeers.begin() + i);
      _groupPeersDirty = true;
      saveGroupPeers();

      // Also remove from the active peer list
      for (size_t j = 0; j < _peers.size(); j++) {
        if (_peers[j].getHost() == hostname) {
          _peers.erase(_peers.begin() + j);
          break;
        }
      }

      DBUGF("LoadSharingGroupState: Removed peer from group: %s (remaining: %u)",
            hostname.c_str(), (unsigned int)_groupPeers.size());

      notifyPeerChange();
      return true;
    }
  }

  DBUGF("LoadSharingGroupState: Peer not found: %s", hostname.c_str());
  return false;
}

bool LoadSharingGroupState::isGroupPeer(const String& hostname) const {
  for (const auto& peer : _groupPeers) {
    if (peer == hostname) {
      return true;
    }
  }
  return false;
}

bool LoadSharingGroupState::isLocalHost(const String& hostname) const {
  // Compare against local hostname (with and without .local suffix)
  String localMdns = esp_hostname + String(".local");
  if (hostname.equalsIgnoreCase(esp_hostname) ||
      hostname.equalsIgnoreCase(localMdns)) {
    return true;
  }
  // Compare against device ID
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
  std::vector<String> addedHosts;

  // Always include the local node first
  {
    PeerInfo local;
    local.hostname = getLocalHostname();
    local.ipAddress = "";
    local.port = www_http_port;
    local.online = true;
    local.joined = true;
    result.push_back(local);
    addedHosts.push_back(local.hostname);
  }

  // Add discovered peers (from mDNS discovery task)
  if (includeDiscovered && _discoveredPeers != nullptr) {
    for (const auto& peer : *_discoveredPeers) {
      // Skip if already added (e.g. local node, though discovery should filter it)
      bool alreadyAdded = false;
      for (const auto& added : addedHosts) {
        if (added.equalsIgnoreCase(peer.hostname)) {
          alreadyAdded = true;
          break;
        }
      }
      if (alreadyAdded) {
        continue;
      }

      PeerInfo info;
      info.hostname = peer.hostname;
      info.ipAddress = peer.ipAddress;
      info.port = peer.port;
      info.online = true;
      info.joined = isGroupPeer(peer.hostname);

      result.push_back(info);
      addedHosts.push_back(peer.hostname);
    }
  }

  // Add group peers that weren't discovered (offline peers)
  if (includeGroup) {
    for (const auto& hostname : _groupPeers) {
      // Check if already added from discovery
      bool alreadyAdded = false;
      for (const auto& added : addedHosts) {
        if (added == hostname) {
          alreadyAdded = true;
          break;
        }
      }

      if (!alreadyAdded) {
        PeerInfo info;
        info.hostname = hostname;
        info.ipAddress = "";
        info.port = www_http_port;
        info.online = false;
        info.joined = true;

        result.push_back(info);
      }
    }
  }

  return result;
}

bool LoadSharingGroupState::loadGroupPeers() {
  const char* filePath = "/loadsharing_peers.json";

  if (!LittleFS.exists(filePath)) {
    DBUGLN("LoadSharingGroupState: No persisted group peer list found, starting with empty list");
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

  _groupPeers.clear();
  _peers.clear();
  JsonArray peers = doc["peers"].as<JsonArray>();
  for (JsonVariant peer : peers) {
    String hostname = peer.as<String>();
    _groupPeers.push_back(hostname);
    // Create corresponding peer entry (initially offline)
    ensurePeerEntry(hostname);
  }

  _groupPeersDirty = false;
  DBUGF("LoadSharingGroupState: Loaded %u group peers", (unsigned int)_groupPeers.size());

  return true;
}

bool LoadSharingGroupState::saveGroupPeers() {
  if (!_groupPeersDirty) {
    return true;  // No changes to save
  }

  const char* filePath = "/loadsharing_peers.json";
  const char* tempPath = "/loadsharing_peers.json.tmp";

  // Write to temp file first (atomic write-rename pattern)
  File file = LittleFS.open(tempPath, "w");
  if (!file) {
    DBUGF("LoadSharingGroupState: Failed to open temp file for writing: %s", tempPath);
    return false;
  }

  DynamicJsonDocument doc(1024);
  JsonArray peers = doc.createNestedArray("peers");
  for (const auto& hostname : _groupPeers) {
    peers.add(hostname);
  }

  if (serializeJson(doc, file) == 0) {
    file.close();
    DBUGLN("LoadSharingGroupState: Failed to write group peer list JSON");
    return false;
  }

  file.close();

  // Atomic rename (replace old with new)
  if (LittleFS.exists(filePath)) {
    LittleFS.remove(filePath);
  }
  if (!LittleFS.rename(tempPath, filePath)) {
    DBUGF("LoadSharingGroupState: Failed to rename temp file to %s", filePath);
    return false;
  }

  _groupPeersDirty = false;
  DBUGF("LoadSharingGroupState: Saved %u group peers", (unsigned int)_groupPeers.size());

  return true;
}
