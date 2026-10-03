#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LOADSHARING_DISCOVERY)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "loadsharing_discovery_task.h"
#include "loadsharing_types.h"
#include "app_config.h"
#include "net_manager.h"
#include <Arduino.h>
#include <espal.h>
#include <MongooseMdns.h>
#include <algorithm>

static String normalizeTxtField(const String& raw) {
  String value = raw;
  value.trim();
  while (value.length() >= 2 &&
         ((value.charAt(0) == '"' && value.charAt(value.length() - 1) == '"') ||
          (value.charAt(0) == '\'' && value.charAt(value.length() - 1) == '\''))) {
    value = value.substring(1, value.length() - 1);
    value.trim();
  }
  return value;
}

// Parse a dotted-quad IPv4 string into a 32-bit host-order value. Returns false
// on malformed input.
static bool parseIpv4(const String& ip, uint32_t& out) {
  uint32_t parts[4] = {0, 0, 0, 0};
  int part = 0;
  int value = -1;
  for (size_t i = 0; i <= (size_t)ip.length(); i++) {
    char c = (i < (size_t)ip.length()) ? ip.charAt(i) : '.';
    if (c == '.') {
      if (value < 0 || value > 255 || part > 3) return false;
      parts[part++] = (uint32_t)value;
      value = -1;
    } else if (c >= '0' && c <= '9') {
      value = (value < 0 ? 0 : value) * 10 + (c - '0');
    } else {
      return false;
    }
  }
  if (part != 4) return false;
  out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
  return true;
}

// True when addr is on the same IPv4 subnet as the local device. When the local
// IP or netmask is unknown (e.g. not yet connected, or a platform that does not
// report a mask), returns false so callers fall back to their default choice.
static bool isSameSubnet(const String& addr) {
  String localIp = net.getIp();
  String mask = net.getNetmask();
  uint32_t a, l, m;
  if (!parseIpv4(addr, a) || !parseIpv4(localIp, l) || !parseIpv4(mask, m)) {
    return false;
  }
  if (m == 0) return false;  // No usable mask
  return (a & m) == (l & m);
}

// Global instance
LoadSharingDiscoveryTask loadSharingDiscoveryTask;

LoadSharingDiscoveryTask::LoadSharingDiscoveryTask(unsigned long cacheTtl,
                                                   unsigned long poll_interval_ms,
                                                   unsigned long discovery_interval_ms,
                                                   unsigned long query_timeout_ms)
  : MicroTasks::Task(),
    _cacheTtl(cacheTtl),
    _lastDiscovery(0),
    _cacheValid(false),
    _poll_interval_ms(poll_interval_ms),
    _discovery_interval_ms(discovery_interval_ms),
    _query_timeout_ms(query_timeout_ms),
    _last_discovery_time(0),
    _query_start_time(0),
    _query_in_progress(false),
    _manual_trigger(false),
    _groupState(nullptr),
    _discovery_count(0),
    _last_result_count(0)
{
}

void LoadSharingDiscoveryTask::setup() {
  // Task is ready to run
  DBUGF("LoadSharingDiscoveryTask: Setup complete");
}

unsigned long LoadSharingDiscoveryTask::loop(MicroTasks::WakeReason reason) {
  unsigned long now = millis();

  // If a query is currently in progress, poll its status
  if (_query_in_progress) {
    if (pollAsyncQuery()) {
      // Results are a snapshot; release the library's browse state.
      cleanupQuery();
    }
  } else {
    // No query in progress, check if we should start a new one. Periodic
    // discovery only runs while load sharing is enabled: with it off nothing
    // consumes the results, and every query is an mDNS round that every other
    // OpenEVSE on the LAN answers plus a burst of heap churn on no-PSRAM
    // boards. A manual trigger still runs a single query so the UI can browse
    // for peers before enabling.
    bool periodic_due = loadsharing_enabled &&
                        (now - _last_discovery_time >= _discovery_interval_ms || _last_discovery_time == 0);
    if (_manual_trigger || periodic_due) {
      _manual_trigger = false;
      DBUGF("LoadSharingDiscoveryTask: Starting discovery iteration %lu", _discovery_count + 1);
      startAsyncQuery();
      _last_discovery_time = now;
      _discovery_count++;
    }
  }

  // Always wake up at next poll interval
  return _poll_interval_ms;
}

void LoadSharingDiscoveryTask::begin(LoadSharingGroupState& groupState) {
  _groupState = &groupState;

  _lastDiscovery = 0;  // Invalidate cache
  _last_discovery_time = 0;  // Force immediate first discovery

  MicroTask.startTask(this);
  DBUGF("LoadSharingDiscoveryTask: Started background discovery");
}

void LoadSharingDiscoveryTask::end() {
  cleanupQuery();
  MicroTask.stopTask(this);
  DBUGF("LoadSharingDiscoveryTask: Stopped background discovery");
}

void LoadSharingDiscoveryTask::triggerDiscovery() {
  // Run one query on the next task wake, regardless of the enabled flag
  _manual_trigger = true;
  MicroTask.wakeTask(this);
  DBUGF("LoadSharingDiscoveryTask: Triggered manual discovery");
}

const std::vector<DiscoveredPeer>& LoadSharingDiscoveryTask::getCachedPeers() const {
  return _cachedPeers;
}

bool LoadSharingDiscoveryTask::isCacheValid() const {
  if (!_cacheValid) {
    return false;
  }

  return (millis() - _lastDiscovery < _cacheTtl);
}

void LoadSharingDiscoveryTask::invalidateCache() {
  _cacheValid = false;
  _cachedPeers.clear();
  DBUGLN("Discovery cache invalidated");
}

unsigned long LoadSharingDiscoveryTask::cacheTimeRemaining() const {
  if (!_cacheValid) {
    return 0;
  }

  unsigned long elapsed = millis() - _lastDiscovery;
  if (elapsed >= _cacheTtl) {
    return 0;
  }

  return _cacheTtl - elapsed;
}

void LoadSharingDiscoveryTask::startAsyncQuery() {
  if (Mdns.browse("_openevse._tcp")) {
    _query_in_progress = true;
    _query_start_time = millis();
    DBUGLN("LoadSharingDiscoveryTask: Async query started");
  } else {
    DBUGLN("LoadSharingDiscoveryTask: ERROR - Failed to start async query");
    _query_in_progress = false;
  }
}

bool LoadSharingDiscoveryTask::pollAsyncQuery() {
  if (!_query_in_progress) {
    return false;
  }

  // Mongoose receives and assembles DNS-SD records in its normal event loop.
  // Allow the whole browse window for additional peers and split responses.
  if ((long)(millis() - (_query_start_time + _query_timeout_ms)) >= 0) {
    unsigned long elapsed = millis() - _query_start_time;

    auto hasUsableIp = [](const String& ip) -> bool {
      return !ip.isEmpty() && ip != "0.0.0.0";
    };

    auto mergePeer = [&](DiscoveredPeer& dst, const DiscoveredPeer& src) {
      if (dst.serviceName.isEmpty() && !src.serviceName.isEmpty()) dst.serviceName = src.serviceName;
      if (dst.hostname.isEmpty() && !src.hostname.isEmpty()) dst.hostname = src.hostname;
      // Choose the peer address, preferring one on our own subnet. An mDNS host
      // often advertises several interface addresses (docker bridges, VPNs,
      // secondary NICs); only a same-subnet address is reliably routable, so a
      // same-subnet candidate upgrades whatever we picked first.
      if (hasUsableIp(src.ipAddress)) {
        bool dstUsable = hasUsableIp(dst.ipAddress);
        bool srcSameSubnet = isSameSubnet(src.ipAddress);
        bool dstSameSubnet = dstUsable && isSameSubnet(dst.ipAddress);
        if (!dstUsable || (srcSameSubnet && !dstSameSubnet)) {
          dst.ipAddress = src.ipAddress;
        }
      }
      if (dst.port == 0 && src.port > 0) dst.port = src.port;
      if (dst.id.isEmpty() && !src.id.isEmpty()) dst.id = src.id;
      if (dst.name.isEmpty() && !src.name.isEmpty()) dst.name = src.name;
      if (dst.url.isEmpty() && !src.url.isEmpty()) dst.url = src.url;
      for (const auto& kv : src.txtRecords) {
        dst.txtRecords[kv.first] = kv.second;
      }
    };

    // Convert the library's DNS-SD snapshot into application peer records.
    std::vector<DiscoveredPeer> peers;
    std::vector<String> seenHostnames;  // Track to deduplicate/merge

    for (const auto& r : Mdns.services()) {
      DiscoveredPeer peer;

      if (r.hostname.empty() || r.port == 0) continue;
      peer.hostname = r.hostname.c_str();
      peer.serviceName = r.instance.c_str();
      const String suffix = "._openevse._tcp.local";
      if (peer.serviceName.endsWith(suffix))
        peer.serviceName.remove(peer.serviceName.length() - suffix.length());
      String resolvedHost = peer.hostname;

      // Extract IP address. A responder lists every address it has, IPv6
      // included (link-local and ULA AAAA records come back alongside the A
      // record), and the list order is not ours to rely on. Prefer a same-subnet
      // IPv4 entry, falling back to the first IPv4 address.
      for (const auto& address : r.addresses) {
        if (address.is_ip6) continue;
        char ip[16];
        mg_snprintf(ip, sizeof(ip), "%M", mg_print_ip, &address);
        if (!hasUsableIp(peer.ipAddress) || isSameSubnet(ip)) peer.ipAddress = ip;
        if (isSameSubnet(peer.ipAddress)) break;
      }

      peer.port = r.port;
      peer.discoveredAt = millis();

      // Extract TXT records
      for (const auto& txt : r.txt) {
        String key = normalizeTxtField(String(txt.first.c_str()));
        String value = normalizeTxtField(String(txt.second.c_str()));
        if (!key.isEmpty()) {
          peer.txtRecords[key] = value;
        }
      }

      // Filter out the local instance by device ID (TXT "id" record) or hostname
      String localId = ESPAL.getLongId();
      String localHostname = esp_hostname + String(".local");
      auto idIt = peer.txtRecords.find(String("id"));
      if (idIt != peer.txtRecords.end() && idIt->second == localId) {
        DBUGF("  Skipping self (matched device id): %s", peer.hostname.c_str());
        continue;
      }
      if (peer.hostname.equalsIgnoreCase(localHostname)) {
        DBUGF("  Skipping self (matched hostname): %s", peer.hostname.c_str());
        continue;
      }

      DBUGF("  Found peer: %s (%s:%u)",
            peer.hostname.c_str(), peer.ipAddress.c_str(), peer.port);

      // Build URL from discovered port and ssl TXT record
      bool ssl = false;
      auto sslIt = peer.txtRecords.find("ssl");
      if (sslIt != peer.txtRecords.end() && sslIt->second == "1") {
        ssl = true;
      }

      // Prefer an actual resolved IP for URLs (native mode may advertise
      // instance names that are not resolvable via .local DNS).
      String urlHost = hasUsableIp(peer.ipAddress)
                           ? peer.ipAddress
                           : (!resolvedHost.isEmpty() ? resolvedHost : peer.hostname);

      peer.url = ssl ? "https://" : "http://";
      peer.url += urlHost;
      if (peer.port > 0) {
        if ((ssl && peer.port != 443) || (!ssl && peer.port != 80)) {
          peer.url += ":" + String(peer.port);
        }
      }

      // Extract device ID from TXT records
      auto idIt2 = peer.txtRecords.find("id");
      if (idIt2 != peer.txtRecords.end() && !idIt2->second.isEmpty()) {
        peer.id = idIt2->second;
      }

      // Build display name (strip .local suffix)
      peer.name = peer.hostname;
      if (peer.name.endsWith(".local")) {
        peer.name.remove(peer.name.length() - 6, 6);
      }

      auto it = std::find(seenHostnames.begin(), seenHostnames.end(), peer.hostname);
      if (it != seenHostnames.end()) {
        size_t index = (size_t)std::distance(seenHostnames.begin(), it);
        mergePeer(peers[index], peer);
      } else {
        seenHostnames.push_back(peer.hostname);
        peers.push_back(peer);
      }
    }

    // Rebuild each URL from the finally-chosen IP. The per-result URL above is
    // built before merge, so a merge that upgraded the address to a same-subnet
    // IP would otherwise leave the URL pointing at the earlier (possibly
    // unroutable) address. Only rebuild when we have a usable IP; keep the
    // hostname-based URL as a fallback otherwise.
    for (auto& peer : peers) {
      if (!hasUsableIp(peer.ipAddress)) continue;
      bool ssl = false;
      auto sslIt = peer.txtRecords.find("ssl");
      if (sslIt != peer.txtRecords.end() && sslIt->second == "1") {
        ssl = true;
      }
      String url = ssl ? "https://" : "http://";
      url += peer.ipAddress;
      if (peer.port > 0 &&
          ((ssl && peer.port != 443) || (!ssl && peer.port != 80))) {
        url += ":" + String(peer.port);
      }
      peer.url = url;
    }

    DBUGF("LoadSharingDiscoveryTask: Query complete in %lu ms, found %u peers",
          elapsed, (unsigned int)peers.size());

    // Update our cache with the new results
    _cachedPeers = peers;
    _lastDiscovery = millis();
    _cacheValid = true;

    processQueryResults(peers);

    return true;  // Query is complete
  }

  return false;  // Query still in progress
}

void LoadSharingDiscoveryTask::processQueryResults(const std::vector<DiscoveredPeer>& peers) {
  _last_result_count = peers.size();

  if (_groupState) {
    _groupState->onDiscoveryComplete(peers);
  }

  DBUGF("LoadSharingDiscoveryTask: Processed %u peer discovery results", (unsigned int)peers.size());
}

void LoadSharingDiscoveryTask::cleanupQuery() {
  Mdns.cancelBrowse();
  _query_in_progress = false;
}
