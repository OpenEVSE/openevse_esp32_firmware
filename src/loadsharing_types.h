#ifndef _LOADSHARING_TYPES_H
#define _LOADSHARING_TYPES_H

#include <Arduino.h>
#include <vector>

// Forward declarations
class LoadSharingPeerStatus;
class LoadSharingPeer;
class LoadSharingAllocation;
class LoadSharingGroupState;
struct DiscoveredPeer;

/**
 * @brief Minimal peer status snapshot used for load sharing allocation decisions.
 *
 * This matches the LoadSharingPeerStatus schema from api.yml.
 */
class LoadSharingPeerStatus {
private:
  double _amp;           // Measured current (amps)
  double _voltage;       // Measured voltage (volts)
  double _pilot;         // Current pilot setpoint (amps)
  uint8_t _vehicle;      // 1 if vehicle connected, else 0
  uint8_t _state;        // EVSE state code (J1772 state)
  uint32_t _config_version;  // Config version for sync detection
  String _config_hash;   // Config hash for mismatch detection

public:
  LoadSharingPeerStatus() :
    _amp(0.0),
    _voltage(0.0),
    _pilot(0.0),
    _vehicle(0),
    _state(0),
    _config_version(0),
    _config_hash("")
  {}

  // Accessors
  double getAmp() const { return _amp; }
  void setAmp(double value) { _amp = value; }

  double getVoltage() const { return _voltage; }
  void setVoltage(double value) { _voltage = value; }

  double getPilot() const { return _pilot; }
  void setPilot(double value) { _pilot = value; }

  uint8_t getVehicle() const { return _vehicle; }
  void setVehicle(uint8_t value) { _vehicle = value; }

  uint8_t getState() const { return _state; }
  void setState(uint8_t value) { _state = value; }

  uint32_t getConfigVersion() const { return _config_version; }
  void setConfigVersion(uint32_t value) { _config_version = value; }

  String getConfigHash() const { return _config_hash; }
  void setConfigHash(const String& value) { _config_hash = value; }
};

/**
 * @brief Represents a discovered or configured peer in the load sharing group.
 *
 * This matches the LoadSharingPeer schema from api.yml with additional
 * runtime tracking fields.
 */
class LoadSharingPeer {
private:
  String _id;            // Stable device id (e.g., "openevse-a7d4")
  String _name;          // mDNS instance/hostname if available
  String _host;          // Hostname or IP address used to reach the peer
  uint16_t _port;        // Service port (0 = default 80)
  String _ip;            // Current resolved IP address
  String _version;       // Firmware version if available
  bool _online;          // Is peer currently reachable?
  unsigned long _last_seen;  // millis() timestamp of last successful contact
  LoadSharingPeerStatus _status;  // Latest status snapshot

public:
  LoadSharingPeer() :
    _id(""),
    _name(""),
    _host(""),
    _port(0),
    _ip(""),
    _version(""),
    _online(false),
    _last_seen(0),
    _status()
  {}

  LoadSharingPeer(const String& host_) :
    _id(""),
    _name(""),
    _host(host_),
    _port(0),
    _ip(""),
    _version(""),
    _online(false),
    _last_seen(0),
    _status()
  {}

  // Accessors
  String getId() const { return _id; }
  void setId(const String& value) { _id = value; }

  String getName() const { return _name; }
  void setName(const String& value) { _name = value; }

  String getHost() const { return _host; }
  void setHost(const String& value) { _host = value; }

  uint16_t getPort() const { return _port; }
  void setPort(uint16_t value) { _port = value; }

  /** Build "host" or "host:port" suitable for HTTP URLs. */
  String getHostPort() const {
    if (_port == 0 || _port == 80) return _host;
    return _host + ":" + String(_port);
  }

  String getIp() const { return _ip; }
  void setIp(const String& value) { _ip = value; }

  String getVersion() const { return _version; }
  void setVersion(const String& value) { _version = value; }

  bool isOnline() const { return _online; }
  void setOnline(bool value) { _online = value; }

  unsigned long getLastSeen() const { return _last_seen; }
  void setLastSeen(unsigned long value) { _last_seen = value; }

  LoadSharingPeerStatus& getStatus() { return _status; }
  const LoadSharingPeerStatus& getStatus() const { return _status; }
  void setStatus(const LoadSharingPeerStatus& value) { _status = value; }
};

/**
 * @brief Per-member current allocation result from the allocation algorithm.
 *
 * This matches the LoadSharingAllocation schema from api.yml.
 */
class LoadSharingAllocation {
private:
  String _id;            // Member id (device_id)
  double _target_current; // Allocated current (amps)
  String _reason;        // Human-readable reason for allocation (e.g., "equal_share", "offline")

public:
  LoadSharingAllocation() :
    _id(""),
    _target_current(0.0),
    _reason("")
  {}

  LoadSharingAllocation(const String& id_, double target_current_, const String& reason_ = "") :
    _id(id_),
    _target_current(target_current_),
    _reason(reason_)
  {}

  // Accessors
  String getId() const { return _id; }
  void setId(const String& value) { _id = value; }

  double getTargetCurrent() const { return _target_current; }
  void setTargetCurrent(double value) { _target_current = value; }

  String getReason() const { return _reason; }
  void setReason(const String& value) { _reason = value; }
};

/**
 * @brief Runtime state for the load sharing group.
 *
 * This is the in-memory representation used by the allocation algorithm
 * and status reporting. Includes both configured peers and computed allocations.
 */
class LoadSharingGroupState {
public:
  /**
   * @brief Callback type for peer list change notifications.
   * Called when peers are added, removed, or change online status.
   */
  typedef void (*PeerChangeCallback)();

private:
  // Configuration (from app_config)
  bool _enabled;         // Is load sharing enabled?
  String _group_id;      // Group identifier
  double _group_max_current; // Total circuit limit (amps)
  double _safety_factor; // Safety multiplier (0-1)

  // Peer list - the authoritative merged list of active group members
  // Populated from _groupPeers and updated with discovery results
  std::vector<LoadSharingPeer> _peers;

  // Group peers (manually added hostnames, persisted to LittleFS)
  std::vector<String> _groupPeers;
  bool _groupPeersDirty;

  // Pointer to discovery task's cached peers (set by discovery task in begin)
  const std::vector<DiscoveredPeer>* _discoveredPeers;

  // Computed allocations (result of allocation algorithm)
  std::vector<LoadSharingAllocation> _allocations;

  // Runtime status
  unsigned long _computed_at;    // millis() timestamp of last allocation computation
  bool _failsafe_active;         // Is failsafe mode currently active?
  uint8_t _online_count;         // Number of peers currently online
  uint8_t _offline_count;        // Number of peers currently offline
  bool _config_consistent;       // Are all peer configs consistent?
  std::vector<String> _config_issues; // List of detected config mismatches

  // Peer change notification
  PeerChangeCallback _onPeerChange;

  /**
   * @brief Notify listeners that the peer list has changed.
   */
  void notifyPeerChange();

  /**
   * @brief Ensure a LoadSharingPeer entry exists in _peers for a hostname.
   * Creates one if it doesn't exist.
   */
  void ensurePeerEntry(const String& hostname);

public:
  LoadSharingGroupState() :
    _enabled(false),
    _group_id(""),
    _group_max_current(0.0),
    _safety_factor(1.0),
    _peers(),
    _groupPeers(),
    _groupPeersDirty(false),
    _discoveredPeers(nullptr),
    _allocations(),
    _computed_at(0),
    _failsafe_active(false),
    _online_count(0),
    _offline_count(0),
    _config_consistent(true),
    _config_issues(),
    _onPeerChange(nullptr)
  {}

  // Accessors
  bool isEnabled() const { return _enabled; }
  void setEnabled(bool value) { _enabled = value; }

  String getGroupId() const { return _group_id; }
  void setGroupId(const String& value) { _group_id = value; }

  double getGroupMaxCurrent() const { return _group_max_current; }
  void setGroupMaxCurrent(double value) { _group_max_current = value; }

  double getSafetyFactor() const { return _safety_factor; }
  void setSafetyFactor(double value) { _safety_factor = value; }

  std::vector<LoadSharingPeer>& getPeers() { return _peers; }
  const std::vector<LoadSharingPeer>& getPeers() const { return _peers; }

  std::vector<LoadSharingAllocation>& getAllocations() { return _allocations; }
  const std::vector<LoadSharingAllocation>& getAllocations() const { return _allocations; }

  unsigned long getComputedAt() const { return _computed_at; }
  void setComputedAt(unsigned long value) { _computed_at = value; }

  bool isFailsafeActive() const { return _failsafe_active; }
  void setFailsafeActive(bool value) { _failsafe_active = value; }

  uint8_t getOnlineCount() const { return _online_count; }
  void setOnlineCount(uint8_t value) { _online_count = value; }

  uint8_t getOfflineCount() const { return _offline_count; }
  void setOfflineCount(uint8_t value) { _offline_count = value; }

  bool isConfigConsistent() const { return _config_consistent; }
  void setConfigConsistent(bool value) { _config_consistent = value; }

  std::vector<String>& getConfigIssues() { return _config_issues; }
  const std::vector<String>& getConfigIssues() const { return _config_issues; }

  /**
   * @brief Get peer by host (hostname or IP).
   * @return Pointer to peer if found, nullptr otherwise.
   */
  LoadSharingPeer* getPeerByHost(const String& host) {
    for (auto& peer : _peers) {
      if (peer.getHost() == host) {
        return &peer;
      }
    }
    return nullptr;
  }

  /**
   * @brief Get peer by device id.
   * @return Pointer to peer if found, nullptr otherwise.
   */
  LoadSharingPeer* getPeerById(const String& id) {
    for (auto& peer : _peers) {
      if (peer.getId() == id) {
        return &peer;
      }
    }
    return nullptr;
  }

  /**
   * @brief Add or update a peer in the group.
   * @return true if peer was added, false if updated.
   */
  bool addOrUpdatePeer(const LoadSharingPeer& peer) {
    LoadSharingPeer* existing = getPeerByHost(peer.getHost());
    if (existing) {
      // Update existing peer
      existing->setId(peer.getId());
      existing->setName(peer.getName());
      existing->setIp(peer.getIp());
      existing->setVersion(peer.getVersion());
      existing->setOnline(peer.isOnline());
      existing->setLastSeen(peer.getLastSeen());
      existing->setStatus(peer.getStatus());
      return false;
    } else {
      // Add new peer
      _peers.push_back(peer);
      return true;
    }
  }

  /**
   * @brief Remove a peer by host.
   * @return true if peer was removed, false if not found.
   */
  bool removePeer(const String& host) {
    for (auto it = _peers.begin(); it != _peers.end(); ++it) {
      if (it->getHost() == host) {
        _peers.erase(it);
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Update online/offline counts based on peer status.
   */
  void updateCounts() {
    _online_count = 0;
    _offline_count = 0;
    for (const auto& peer : _peers) {
      if (peer.isOnline()) {
        _online_count++;
      } else {
        _offline_count++;
      }
    }
  }

  /**
   * @brief Clear all allocations.
   */
  void clearAllocations() {
    _allocations.clear();
  }

  /**
   * @brief Get allocation for a specific peer by device id.
   * @return Pointer to allocation if found, nullptr otherwise.
   */
  LoadSharingAllocation* getAllocationById(const String& id) {
    for (auto& alloc : _allocations) {
      if (alloc.getId() == id) {
        return &alloc;
      }
    }
    return nullptr;
  }

  // =========================================================================
  // Peer change notification
  // =========================================================================

  /**
   * @brief Register a callback for peer list changes.
   * Called when peers are added, removed, or change online/offline status.
   * Typically used to wake the peer poller task.
   */
  void setOnPeerChange(PeerChangeCallback cb) { _onPeerChange = cb; }

  // =========================================================================
  // Discovery integration
  // =========================================================================

  /**
   * @brief Set reference to discovery task's cached peer list.
   * Called by LoadSharingDiscoveryTask::begin() to wire discovery results.
   */
  void setDiscoveredPeers(const std::vector<DiscoveredPeer>* discoveredPeers) {
    _discoveredPeers = discoveredPeers;
  }

  /**
   * @brief Called by discovery task after each mDNS query completes.
   * Updates online/offline status and IP addresses for group peers
   * based on discovery results, then notifies the peer poller.
   */
  void onDiscoveryComplete();

  // =========================================================================
  // Group peer management (persisted to LittleFS)
  // =========================================================================

  /**
   * @brief Add a peer to the group.
   * Also creates an entry in the active peer list and notifies listeners.
   * @param hostname Hostname or IP address of the peer
   * @return true if added successfully, false if already exists
   */
  bool addGroupPeer(const String& hostname);

  /**
   * @brief Remove a peer from the group.
   * Also removes from the active peer list and notifies listeners.
   * @param hostname Hostname or IP address of the peer
   * @return true if removed successfully, false if not found
   */
  bool removeGroupPeer(const String& hostname);

  /**
   * @brief Get list of group peer hostnames.
   */
  const std::vector<String>& getGroupPeers() const { return _groupPeers; }

  /**
   * @brief Check if a hostname is in the group.
   */
  bool isGroupPeer(const String& hostname) const;

  /**
   * @brief Check if a hostname refers to the local device.
   * Compares against esp_hostname, esp_hostname + ".local", and device ID.
   * @param hostname Hostname, mDNS name, or device ID to check
   * @return true if it matches the local device
   */
  bool isLocalHost(const String& hostname) const;

  /**
   * @brief Get the local device's mDNS hostname (e.g., "openevse-abcd.local").
   */
  String getLocalHostname() const;

  /**
   * @brief Load group peers from LittleFS.
   * Also populates the active peer list from loaded hostnames.
   * @return true if loaded successfully
   */
  bool loadGroupPeers();

  /**
   * @brief Save group peers to LittleFS.
   * @return true if saved successfully
   */
  bool saveGroupPeers();

  /**
   * @brief Unified peer info for API responses.
   */
  struct PeerInfo {
    String hostname;
    String ipAddress;
    uint16_t port;  // Advertised service port (0 = default 80)
    bool online;    // True if discovered via mDNS
    bool joined;    // True if in group
  };

  /**
   * @brief Get unified peer list (discovered + group offline peers + local node).
   * The local node is always included first with joined=true, online=true.
   * Helper for GET /loadsharing/peers endpoint.
   *
   * @param includeDiscovered Include mDNS-discovered peers (default: true)
   * @param includeGroup Include manually-added group peers (default: true)
   * @return Vector of peer info with online/joined status
   */
  std::vector<PeerInfo> getAllPeers(bool includeDiscovered = true, bool includeGroup = true) const;
};

/**
 * @brief Global instance of the load sharing group state
 */
extern LoadSharingGroupState loadSharingGroupState;

#endif // _LOADSHARING_TYPES_H
