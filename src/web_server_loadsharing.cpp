#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_WEB)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include "web_server.h"
#include "app_config.h"
#include "loadsharing_discovery_task.h"
#include "loadsharing_peer_poller.h"
#include "loadsharing_types.h"
#include "debug.h"
#include <vector>
#include <ArduinoJson.h>

typedef const __FlashStringHelper *fstr_t;

// Path prefix constants
#define LOADSHARING_PEERS_PATH "/loadsharing/peers"
#define LOADSHARING_PEERS_PATH_LEN (sizeof(LOADSHARING_PEERS_PATH) - 1)

// Forward declarations
void handleLoadSharingPeersGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response);
void handleLoadSharingPeersPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response);
void handleLoadSharingPeersDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response);
void handleLoadSharingPeersDeleteWithHost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, const String &host);
void handleLoadSharingDiscover(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response);
void handleLoadSharingStatus(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response);

// -------------------------------------------------------------------
//
// url: /loadsharing/peers
// GET - list discovered and configured peers
// POST - add a new peer
// DELETE - remove a peer
//
// -------------------------------------------------------------------

void handleLoadSharingPeers(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleLoadSharingPeersGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleLoadSharingPeersPost(request, response);
  } else if(HTTP_DELETE == request->method()) {
    handleLoadSharingPeersDelete(request, response);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}

void handleLoadSharingPeersGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  DBUGLN("[LoadSharing] GET /loadsharing/peers");

  // Build JSON response array
  const size_t capacity = JSON_ARRAY_SIZE(20) + JSON_OBJECT_SIZE(6) * 20 + 1024;
  DynamicJsonDocument doc(capacity);
  JsonArray peerArray = doc.to<JsonArray>();

  // Get unified peer list from group state
  std::vector<LoadSharingGroupState::PeerInfo> peers = loadSharingGroupState.getAllPeers();

  DBUGF("[LoadSharing] Found %u total peers", peers.size());

  // Add all peers to response
  for(const auto &peer : peers) {
    DBUGF("[LoadSharing] Adding peer: %s (online: %s, joined: %s)",
          peer.hostname.c_str(), peer.online ? "yes" : "no", peer.joined ? "yes" : "no");

    JsonObject peerObj = peerArray.createNestedObject();
    peerObj["id"] = "unknown";
    peerObj["name"] = peer.hostname;
    peerObj["host"] = peer.hostname;
    peerObj["ip"] = peer.ipAddress;
    peerObj["online"] = peer.online;
    peerObj["joined"] = peer.joined;
  }

  response->setCode(200);
  serializeJson(doc, *response);
}

void handleLoadSharingPeersPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  DBUGLN("[LoadSharing] POST /loadsharing/peers");

  // Parse request body
  String body = request->body().toString();
  DBUGF("[LoadSharing] Request body: %s", body.c_str());

  const size_t capacity = JSON_OBJECT_SIZE(2) + 256;
  DynamicJsonDocument doc(capacity);
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    DBUGF("[LoadSharing] JSON parse error: %s", error.c_str());
    response->setCode(400);
    response->print("{\"msg\":\"Invalid JSON\"}");
    return;
  }

  // Get the host parameter
  if (!doc.containsKey("host")) {
    DBUGLN("[LoadSharing] Missing 'host' parameter");
    response->setCode(400);
    response->print("{\"msg\":\"Missing required 'host' parameter\"}");
    return;
  }

  String host = doc["host"].as<String>();
  host.trim();

  if (host.isEmpty()) {
    DBUGLN("[LoadSharing] Empty host parameter");
    response->setCode(400);
    response->print("{\"msg\":\"Host cannot be empty\"}");
    return;
  }

  DBUGF("[LoadSharing] Adding peer: %s", host.c_str());

  // Validate host is resolvable (basic check)
  if (host.indexOf('.') == -1 && host.indexOf(':') == -1) {
    DBUGF("[LoadSharing] Invalid host format: %s", host.c_str());
    response->setCode(400);
    response->print("{\"msg\":\"Invalid host format - must contain domain or IP\"}");
    return;
  }

  // Add to group peers list via group state
  if (!loadSharingGroupState.addGroupPeer(host)) {
    DBUGF("[LoadSharing] Peer already in group: %s", host.c_str());
    response->setCode(400);
    response->print("{\"msg\":\"Peer already in group\"}");
    return;
  }

  DBUGF("[LoadSharing] Peer added successfully. Total in group: %u",
        (unsigned int)loadSharingGroupState.getGroupPeers().size());

  response->setCode(200);
  response->print("{\"msg\":\"done\"}");
}

void handleLoadSharingPeersDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  // TODO: Phase 1.4 - Remove peer from configured group
  response->setCode(501);
  response->print("{\"msg\":\"Not implemented\"}");
}

void handleLoadSharingPeersDeleteWithHost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response, const String &host)
{
  DBUGF("[LoadSharing] DELETE /loadsharing/peers/%s", host.c_str());

  // Remove peer via group state
  if (!loadSharingGroupState.removeGroupPeer(host)) {
    DBUGF("[LoadSharing] Peer not found: %s", host.c_str());
    response->setCode(404);
    response->print("{\"msg\":\"Peer not found\"}");
    return;
  }

  DBUGF("[LoadSharing] Peer removed. Remaining peers: %u",
        (unsigned int)loadSharingGroupState.getGroupPeers().size());

  // Return success
  response->setCode(200);
  response->print("{\"msg\":\"done\"}");
}

void handleLoadSharingDiscover(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  DBUGLN("[LoadSharing] POST /loadsharing/discover");

  // Trigger manual discovery via the background task
  loadSharingDiscoveryTask.triggerDiscovery();
  DBUGLN("[LoadSharing] Triggered background discovery task");

  response->setCode(200);
  response->print("{\"msg\":\"done\"}");
}

void handleLoadSharingStatus(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  DBUGLN("[LoadSharing] GET /loadsharing/status");

  // Build LoadSharingStatus response
  const size_t capacity = JSON_OBJECT_SIZE(10) + 
                          JSON_ARRAY_SIZE(20) +  // peers array
                          JSON_ARRAY_SIZE(20) +  // allocations array
                          JSON_ARRAY_SIZE(10) +  // config_issues array
                          JSON_OBJECT_SIZE(4) * 20 +  // peer objects
                          JSON_OBJECT_SIZE(3) * 20 +  // allocation objects
                          2048;  // extra buffer
  DynamicJsonDocument doc(capacity);

  // Get current timestamp in ISO 8601 format
  time_t now = time(nullptr);
  struct tm *timeinfo = gmtime(&now);
  char computed_at_str[30];
  strftime(computed_at_str, sizeof(computed_at_str), "%Y-%m-%dT%H:%M:%SZ", timeinfo);

  // Add basic status fields
  doc["enabled"] = loadSharingGroupState.isEnabled();
  doc["group_id"] = loadSharingGroupState.getGroupId();
  doc["computed_at"] = computed_at_str;
  doc["failsafe_active"] = loadSharingGroupState.isFailsafeActive();
  doc["online_count"] = loadSharingGroupState.getOnlineCount();
  doc["offline_count"] = loadSharingGroupState.getOfflineCount();

  // Add peers array
  JsonArray peersArray = doc.createNestedArray("peers");
  
  // Get all peers (discovered and configured)
  std::vector<LoadSharingGroupState::PeerInfo> peersList = loadSharingGroupState.getAllPeers();
  
  for (const auto& peerInfo : peersList) {
    JsonObject peerObj = peersArray.createNestedObject();
    
    // Find the full peer object to get more details
    LoadSharingPeer* fullPeer = loadSharingGroupState.getPeerByHost(peerInfo.hostname);
    
    peerObj["id"] = fullPeer ? fullPeer->getId() : "unknown";
    peerObj["name"] = peerInfo.hostname;
    peerObj["host"] = peerInfo.hostname;
    peerObj["ip"] = peerInfo.ipAddress;
    peerObj["online"] = peerInfo.online;
    peerObj["joined"] = peerInfo.joined;
    
    if (fullPeer) {
      peerObj["version"] = fullPeer->getVersion();
      peerObj["last_seen"] = (unsigned int)(fullPeer->getLastSeen() / 1000);  // Convert ms to seconds
      
      // Query peer poller for current status
      LoadSharingPeerStatus peerStatus;
      if (loadSharingPeerPoller.getPeerStatus(peerInfo.hostname, peerStatus)) {
        // Add nested status object from peer poller
        JsonObject statusObj = peerObj.createNestedObject("status");
        statusObj["amp"] = peerStatus.getAmp();
        statusObj["voltage"] = peerStatus.getVoltage();
        statusObj["pilot"] = peerStatus.getPilot();
        statusObj["vehicle"] = peerStatus.getVehicle();
        statusObj["state"] = peerStatus.getState();
      }
    }
  }

  // Add allocations array
  JsonArray allocationsArray = doc.createNestedArray("allocations");
  const std::vector<LoadSharingAllocation>& allocations = loadSharingGroupState.getAllocations();
  
  for (const auto& alloc : allocations) {
    JsonObject allocObj = allocationsArray.createNestedObject();
    allocObj["id"] = alloc.getId();
    allocObj["target_current"] = alloc.getTargetCurrent();
    allocObj["reason"] = alloc.getReason();
  }

  response->setCode(200);
  serializeJson(doc, *response);
}

void web_server_load_sharing_setup()
{
  // Register the /loadsharing/peers endpoint (GET, POST, DELETE)
  server.on("/loadsharing/peers", handleLoadSharingPeers);

  // Register the /loadsharing/peers/{host} endpoint (DELETE with path parameter)
  server.on("/loadsharing/peers/", [](MongooseHttpServerRequest *request) {
    MongooseHttpServerResponseStream *response;
    if(false == requestPreProcess(request, response)) {
      return;
    }

    // Extract the host parameter from the path
    String path = request->uri();
    if(path.length() > LOADSHARING_PEERS_PATH_LEN + 1) {
      String host = path.substring(LOADSHARING_PEERS_PATH_LEN + 1);
      // URL decode the host if needed
      DBUGF("[LoadSharing] DELETE path parameter: %s", host.c_str());

      if(HTTP_DELETE == request->method()) {
        handleLoadSharingPeersDeleteWithHost(request, response, host);
      } else {
        response->setCode(405);
        response->print("{\"msg\":\"Method not allowed\"}");
      }
    } else {
      response->setCode(400);
      response->print("{\"msg\":\"Invalid path\"}");
    }

    request->send(response);
  });

  // Register the /loadsharing/discover endpoint (POST for on-demand discovery)
  server.on("/loadsharing/discover", [](MongooseHttpServerRequest *request) {
    MongooseHttpServerResponseStream *response;
    if(false == requestPreProcess(request, response)) {
      return;
    }

    if(HTTP_POST == request->method()) {
      handleLoadSharingDiscover(request, response);
    } else {
      response->setCode(405);
      response->print("{\"msg\":\"Method not allowed\"}");
    }

    request->send(response);
  });

  // Register the /loadsharing/status endpoint (GET for runtime status)
  server.on("/loadsharing/status", [](MongooseHttpServerRequest *request) {
    MongooseHttpServerResponseStream *response;
    if(false == requestPreProcess(request, response)) {
      return;
    }

    if(HTTP_GET == request->method()) {
      handleLoadSharingStatus(request, response);
    } else {
      response->setCode(405);
      response->print("{\"msg\":\"Method not allowed\"}");
    }

    request->send(response);
  });
}
