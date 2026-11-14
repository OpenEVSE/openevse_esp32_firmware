#ifndef LOADSHARING_PEER_CLIENT_H
#define LOADSHARING_PEER_CLIENT_H

#include <Arduino.h>
#include <functional>

// Receives the Authorization value for the request, or "" to send it unsigned.
typedef std::function<void(const String &authorization)> LoadSharingPeerAuthHandler;

/**
 * @brief Get the credential for one request to a peer.
 *
 * Fetches a nonce from the peer and signs the request with the group key (see
 * loadsharing_auth.h). `ready` is always called exactly once. It runs before
 * this returns when no group key is set, and otherwise from a later network
 * event, so it must look up any peer state afresh rather than capture it.
 *
 * "" means a key-less device, or a peer that does not take peer auth: it has no
 * password, or runs older firmware. The request is still worth sending, because
 * a peer that does need a credential answers 401 and that is what gets reported.
 *
 * @param baseUrl Peer base URL without a trailing slash
 * @param method HTTP method the request will use
 * @param path Request path exactly as it will be sent
 * @param body Request body exactly as it will be sent
 * @param ready Called with the Authorization value
 */
void loadSharingAuthorisePeerRequest(const String &baseUrl, const char *method, const char *path,
                                     const String &body, LoadSharingPeerAuthHandler ready);

#endif // LOADSHARING_PEER_CLIENT_H
