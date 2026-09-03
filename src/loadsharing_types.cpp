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

bool LoadSharingGroupState::removeSoleRemoteGroupPeer() {
  for (auto& peer : _peers) {
    if (peer.isJoined() && !isLocalHost(peer.getHost())) {
      DBUGF("LoadSharingGroupState: Removing sole remote peer from group: %s",
            peer.getHost().c_str());
      peer.setJoined(false);
      saveGroupPeers();
      notifyPeerChange();
      return true;
    }
  }

  DBUGLN("LoadSharingGroupState: No joined remote peer to remove");
  return false;
}

bool LoadSharingGroupState::reconcilePeerId(const String& host) {
  LoadSharingPeer* peer = getPeerByHost(host);
  if (peer == nullptr || peer->getId().isEmpty()) {
    return false;
  }

  const String id = peer->getId();

  // The id also validates who we actually reached. isLocalHost() only knows this
  // device's hostname, mDNS name and id, so adding ourselves by any other
  // reachable address (a bare IP, a DNS alias) gets past it -- and a device
  // sharing load with itself would double-count its own draw. Now that the id is
  // known, drop the entry.
  if (!isLocalHost(peer->getHost()) && id == ESPAL.getLongId()) {
    DBUGF("LoadSharingGroupState: Peer %s is this device (id %s), removing",
          peer->getHost().c_str(), id.c_str());
    for (auto it = _peers.begin(); it != _peers.end(); ++it) {
      if (&(*it) == peer) {
        _peers.erase(it);
        break;
      }
    }
    saveGroupPeers();
    notifyPeerChange();
    return true;
  }

  // Look for another entry for the same device. Compare by address rather than
  // by host so the entry we just updated is never matched against itself.
  LoadSharingPeer* duplicate = nullptr;
  for (auto& candidate : _peers) {
    if (&candidate != peer && candidate.getId() == id) {
      duplicate = &candidate;
      break;
    }
  }
  if (duplicate == nullptr) {
    return false;
  }

  DBUGF("LoadSharingGroupState: Merging duplicate entries for id %s (%s + %s)",
        id.c_str(), peer->getHost().c_str(), duplicate->getHost().c_str());

  // Group membership and the controller-managed priority are the fields a user
  // set deliberately, so they survive from whichever row carries them.
  if (duplicate->isJoined()) {
    peer->setJoined(true);
  }
  if (peer->getPriority() == 0 && duplicate->getPriority() != 0) {
    peer->setPriority(duplicate->getPriority());
  }

  // Keep the surviving entry's own host/url. We only got here because a /config
  // fetch against them succeeded, so they demonstrably reach the device --
  // whereas the discovered addressing may not resolve from here at all (mDNS
  // advertises whatever name the peer knows itself by). Only fill in details the
  // surviving entry is missing.
  if (duplicate->isOnline()) {
    peer->setOnline(true);
  }
  if (peer->getIp().isEmpty() && !duplicate->getIp().isEmpty()) {
    peer->setIp(duplicate->getIp());
  }
  if (peer->getName().isEmpty() && !duplicate->getName().isEmpty()) {
    peer->setName(duplicate->getName());
  }

  for (auto it = _peers.begin(); it != _peers.end(); ++it) {
    if (&(*it) == duplicate) {
      _peers.erase(it);
      break;
    }
  }

  saveGroupPeers();
  notifyPeerChange();
  return true;
}

bool LoadSharingGroupState::setPeerPriority(const String& hostname, int priority) {
  // The local controller is a normal entry; match it via getLocalPeer() so its
  // own priority is editable through the same path as remote peers.
  LoadSharingPeer* peer = isLocalHost(hostname) ? getLocalPeer()
                                                : getPeerByHost(hostname);
  if (!peer) {
    DBUGF("LoadSharingGroupState: setPeerPriority peer not found: %s", hostname.c_str());
    return false;
  }

  peer->setPriority(priority);
  saveGroupPeers();

  DBUGF("LoadSharingGroupState: Set priority for %s to %d", hostname.c_str(), priority);

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
    // Support both the current object form ({id, host, priority}) and the
    // legacy form where each entry was a bare host string.
    String hostname;
    String id;
    int priority = 0;
    if (peer.is<JsonObject>()) {
      hostname = peer["host"].as<String>();
      id = peer["id"].as<String>();
      priority = peer["priority"] | 0;
    } else {
      hostname = peer.as<String>();
    }
    if (hostname.isEmpty() && id.isEmpty()) continue;

    // The local device entry is created first by addLocalPeer(); when the saved
    // list contains the local row, apply its saved priority to that entry
    // rather than adding a duplicate. Priority for the local controller is
    // managed exactly like any other peer.
    if (isLocalHost(hostname) || (!id.isEmpty() && id == ESPAL.getLongId())) {
      LoadSharingPeer* local = getLocalPeer();
      if (local) {
        local->setPriority(priority);
      }
      continue;
    }

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
      existing->setPriority(priority);
      if (existing->getId().isEmpty() && !id.isEmpty()) {
        existing->setId(id);
      }
    } else {
      LoadSharingPeer p(hostname);
      p.setId(id);
      p.setJoined(true);
      p.setPriority(priority);
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
    // Persist joined peers AND the local device entry. The local row carries
    // the controller's own priority, which is managed the same way as every
    // other peer, so it must round-trip through the saved list too.
    if (peer.isJoined()) {
      // Persist the stable device id and host so a peer can be re-matched by id
      // after a restart (discovery may re-key it under a different reachable
      // host), plus the controller-managed priority. Legacy entries stored a
      // bare host string.
      JsonObject obj = peers.createNestedObject();
      obj["id"] = peer.getId();
      obj["host"] = peer.getHost();
      obj["priority"] = peer.getPriority();
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
