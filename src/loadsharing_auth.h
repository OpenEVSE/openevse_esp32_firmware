#pragma once

// Peer-to-peer authentication for load sharing (issue #1172). Pure logic with no
// Arduino/ESP dependency so it can be native unit tested alongside web_auth.
//
// Peers hold a shared group key (loadsharing_group_key) that never goes on the
// wire. The server hands out a single-use nonce from the unauthenticated
// GET /loadsharing/nonce, and the client answers with an HMAC-SHA256 over the
// nonce and the exact request it is making:
//
//   Authorization: OpenEVSE-Peer v1.<nonce>.<mac>
//
// A nonce rather than a timestamp because peer traffic must keep working on a
// site with no internet: a clock-based token would put every peer into failsafe
// after a power cut until SNTP came back. See docs/load-sharing-peer-auth.md.

#include <stddef.h>
#include <stdint.h>
#include <string>

#define LOADSHARING_KEY_MIN_LEN 16
#define LOADSHARING_KEY_MAX_LEN 64

#define LOADSHARING_NONCE_BYTES 16
#define LOADSHARING_NONCE_HEX_LEN (LOADSHARING_NONCE_BYTES * 2)
#define LOADSHARING_NONCE_TTL_MS 15000UL
#define LOADSHARING_NONCE_SLOTS 8

// The value for an Authorization header, signing method + path + nonce + body.
std::string peer_auth_header(const std::string &key, const std::string &method,
                             const std::string &path, const std::string &nonce,
                             const std::string &body);

// True when an Authorization value uses the peer scheme, valid or not.
bool peer_auth_is_peer_header(const std::string &header);

// Split an Authorization value into its nonce and mac. False if malformed.
bool peer_auth_parse(const std::string &header, std::string &nonce, std::string &mac);

// Constant-time check of a mac from peer_auth_parse(). Does not look at the
// nonce's freshness: that is PeerNonceStore::consume().
bool peer_auth_verify(const std::string &key, const std::string &method,
                      const std::string &path, const std::string &nonce,
                      const std::string &body, const std::string &mac);

// A peer credential only reaches what load sharing itself uses. It is not an
// admin credential: every other route still needs www_password.
bool peer_auth_scope_allows(const std::string &method, const std::string &path);

// The fields a peer-authenticated POST /config may carry: what the controller's
// config and reset pushes send. Deliberately excludes the group key and the
// controller-only site budget.
bool peer_auth_config_field_allowed(const char *field);

// Empty (feature off) or LOADSHARING_KEY_MIN_LEN..LOADSHARING_KEY_MAX_LEN chars.
// The mac is computed over known text, so a sniffed exchange allows an offline
// guess at a short key.
bool peer_auth_key_valid(const std::string &key);

// Issued nonces, each usable once. A full store overwrites the oldest entry, so
// a flood of nonce requests can only cost a peer a retry, never grow memory.
// Times are millis(): the unsigned subtraction survives its 49-day rollover.
class PeerNonceStore {
  public:
    void add(const std::string &nonce, uint32_t now);
    bool consume(const std::string &nonce, uint32_t now);

  private:
    struct Slot {
      std::string nonce;
      uint32_t issued = 0;
    };
    Slot _slots[LOADSHARING_NONCE_SLOTS];
    size_t _next = 0;
};
