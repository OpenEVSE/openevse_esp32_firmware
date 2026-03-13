#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LOADSHARING_DISCOVERY)
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "loadsharing_discovery_task.h"
#include "loadsharing_types.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <mdns.h>

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
    _active_query(nullptr),
    _query_start_time(0),
    _query_in_progress(false),
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
      // Query completed, process results handled in pollAsyncQuery()
      _query_in_progress = false;
      _active_query = nullptr;
    } else {
      // Query still in progress, check timeout
      if (now - _query_start_time > _query_timeout_ms) {
        // Query timed out
        DBUGF("LoadSharingDiscoveryTask: Query timeout after %lu ms", _query_timeout_ms);
        cleanupQuery();
        _query_in_progress = false;
      }
    }
  } else {
    // No query in progress, check if we should start a new one
    if (now - _last_discovery_time >= _discovery_interval_ms || _last_discovery_time == 0) {
      // Time to start a new discovery
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

  // Wire the discovered peers pointer into group state
  _groupState->setDiscoveredPeers(&_cachedPeers);

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
  // Reset discovery timer to force immediate query on next task wake
  _last_discovery_time = 0;
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
  // Start an async mDNS query for OpenEVSE services
  // Parameters:
  //   - name: NULL (search for all instances)
  //   - service_type: "openevse" (without leading underscore)
  //   - proto: "tcp" (without leading underscore)
  //   - type: MDNS_TYPE_PTR (PTR record for service discovery)
  //   - timeout_ms: query timeout
  //   - max_results: 20 (collect up to 20 results)
  //   - notifier: NULL (we'll poll instead)
  _active_query = (void*)mdns_query_async_new(
      NULL,
      "openevse",
      "tcp",
      MDNS_TYPE_PTR,
      _query_timeout_ms,
      20,
      NULL
  );

  if (_active_query) {
    _query_in_progress = true;
    _query_start_time = millis();
    DBUGLN("LoadSharingDiscoveryTask: Async query started");
  } else {
    DBUGLN("LoadSharingDiscoveryTask: ERROR - Failed to start async query");
    _query_in_progress = false;
  }
}

bool LoadSharingDiscoveryTask::pollAsyncQuery() {
  if (!_active_query) {
    return false;
  }

  mdns_result_t* results = nullptr;

  // Poll for results with short timeout to avoid blocking
  // Returns true when query is complete (whether or not results were found)
  bool isComplete = mdns_query_async_get_results(
      (mdns_search_once_t*)_active_query,
      100,  // 100ms polling timeout
      &results
  );

  if (isComplete) {
    unsigned long elapsed = millis() - _query_start_time;

    // Convert mdns_result_t linked list to our DiscoveredPeer vector
    std::vector<DiscoveredPeer> peers;
    std::vector<String> seenPeerKeys;  // Track to deduplicate

    for (mdns_result_t* r = results; r; r = r->next) {
      DiscoveredPeer peer;

      // Prefer the mDNS hostname for connectivity, keep service instance separately.
      if (r->hostname) {
        peer.hostname = String(r->hostname);
        if (!peer.hostname.endsWith(".local")) {
          peer.hostname += ".local";
        }
      } else if (r->instance_name) {
        peer.hostname = String(r->instance_name) + String(".local");
      } else {
        continue;  // Skip if no hostname
      }

      if (r->instance_name) {
        peer.serviceName = String(r->instance_name);
      } else {
        peer.serviceName = peer.hostname;
      }

      // Extract IP address
      if (r->addr) {
        uint32_t ip = r->addr->addr.u_addr.ip4.addr;
        peer.ipAddress = String((ip & 0xFF)) + "." +
                        String((ip >> 8) & 0xFF) + "." +
                        String((ip >> 16) & 0xFF) + "." +
                        String((ip >> 24) & 0xFF);
      }

      peer.port = r->port;
      peer.discoveredAt = millis();

      // Extract TXT records
      for (size_t i = 0; i < r->txt_count; i++) {
        if (r->txt[i].key && r->txt[i].value) {
          peer.txtRecords[String(r->txt[i].key)] = String(r->txt[i].value);
        }
      }

      // Filter out the local instance by device ID (TXT "id" record) or hostname
      auto idIt = peer.txtRecords.find(String("id"));
      bool hasId = (idIt != peer.txtRecords.end() && idIt->second.length() > 0);
      bool isSelf = (hasId && _groupState->isLocalHost(idIt->second)) ||
                    (!hasId && _groupState->isLocalHost(peer.hostname));
      if (isSelf) {
        DBUGF("  Skipping self: host=%s id=%s",
              peer.hostname.c_str(),
              hasId ? idIt->second.c_str() : "");
        continue;
      }

      // Dedupe by stable identity first; if unavailable use host:port.
      String dedupeKey;
      if (hasId) {
        dedupeKey = String("id:") + idIt->second;
      } else if (r->instance_name) {
        dedupeKey = String("instance:") + String(r->instance_name);
      } else {
        dedupeKey = String("host:") + peer.hostname + String(":") + String(peer.port);
      }

      bool isDuplicate = false;
      for (const auto &seen : seenPeerKeys) {
        if (seen.equalsIgnoreCase(dedupeKey)) {
          isDuplicate = true;
          break;
        }
      }

      if (isDuplicate) {
        DBUGF("  Skipping duplicate: key=%s", dedupeKey.c_str());
        continue;
      }

      DBUGF("  Found peer: host=%s id=%s ip=%s port=%u",
        peer.hostname.c_str(),
        hasId ? idIt->second.c_str() : "",
        peer.ipAddress.c_str(),
        peer.port);

      seenPeerKeys.push_back(dedupeKey);
      peers.push_back(peer);
    }

    DBUGF("LoadSharingDiscoveryTask: Query complete in %lu ms, found %u peers",
          elapsed, (unsigned int)peers.size());

    // Clean up mDNS results
    if (results) {
      mdns_query_results_free(results);
    }

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
  // Update statistics
  _last_result_count = peers.size();

  // Notify group state so it can update peer online/offline status
  if (_groupState) {
    _groupState->onDiscoveryComplete();
  }

  DBUGF("LoadSharingDiscoveryTask: Processed %u peer discovery results", (unsigned int)peers.size());
}

void LoadSharingDiscoveryTask::cleanupQuery() {
  if (_active_query != nullptr) {
    mdns_query_async_delete((mdns_search_once_t*)_active_query);
    _active_query = nullptr;
  }
  _query_in_progress = false;
}


