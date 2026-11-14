#include "loadsharing_auth.h"
#include "crypto/hmac_sha256.h"
#include "crypto/sha256.h"
#include "web_auth.h"

#include <string.h>

static const std::string SCHEME = "OpenEVSE-Peer ";
static const std::string VER = "v1.";
static const size_t MAC_HEX_LEN = 64;

static std::string to_hex(const uint8_t *data, size_t len) {
  static const char *H = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for(size_t i = 0; i < len; i++) {
    s += H[data[i] >> 4];
    s += H[data[i] & 0xf];
  }
  return s;
}

static bool is_hex(const std::string &s) {
  for(char c : s) {
    if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

static std::string peer_auth_mac(const std::string &key, const std::string &method,
                                 const std::string &path, const std::string &nonce,
                                 const std::string &body) {
  uint8_t body_hash[32];
  sha256((const uint8_t *)body.data(), body.size(), body_hash);

  // Newline separated: none of the fields can contain one once parsed, so a
  // value cannot be shifted across a field boundary to forge another request.
  std::string msg = "oevse-loadsharing-v1\n" + method + "\n" + path + "\n" + nonce + "\n" +
                    to_hex(body_hash, sizeof(body_hash));
  return hmac_sha256_hex(key, msg);
}

std::string peer_auth_header(const std::string &key, const std::string &method,
                             const std::string &path, const std::string &nonce,
                             const std::string &body) {
  return SCHEME + VER + nonce + "." + peer_auth_mac(key, method, path, nonce, body);
}

bool peer_auth_is_peer_header(const std::string &header) {
  return header.compare(0, SCHEME.size(), SCHEME) == 0;
}

bool peer_auth_parse(const std::string &header, std::string &nonce, std::string &mac) {
  const size_t prefix = SCHEME.size() + VER.size();
  if(header.size() != prefix + LOADSHARING_NONCE_HEX_LEN + 1 + MAC_HEX_LEN) return false;
  if(!peer_auth_is_peer_header(header)) return false;
  if(header.compare(SCHEME.size(), VER.size(), VER) != 0) return false;
  if(header[prefix + LOADSHARING_NONCE_HEX_LEN] != '.') return false;

  nonce = header.substr(prefix, LOADSHARING_NONCE_HEX_LEN);
  mac = header.substr(prefix + LOADSHARING_NONCE_HEX_LEN + 1);
  return is_hex(nonce) && is_hex(mac);
}

bool peer_auth_verify(const std::string &key, const std::string &method,
                      const std::string &path, const std::string &nonce,
                      const std::string &body, const std::string &mac) {
  if(key.empty()) return false;
  return auth_constant_time_equals(mac, peer_auth_mac(key, method, path, nonce, body));
}

bool peer_auth_scope_allows(const std::string &method, const std::string &path) {
  if(method == "GET") {
    return path == "/status" || path == "/config" || path == "/ws";
  }
  if(method == "POST") {
    return path == "/config" || path == "/loadsharing/peers";
  }
  return false;
}

bool peer_auth_config_field_allowed(const char *field) {
  static const char *ALLOWED[] = {
    "loadsharing_enabled",
    "loadsharing_role",
    "loadsharing_controller_host",
    "loadsharing_group_id",
    "loadsharing_safety_factor",
    "loadsharing_heartbeat_timeout",
    "loadsharing_failsafe_mode",
    "loadsharing_failsafe_safe_current",
    "loadsharing_failsafe_peer_assumed_current",
  };
  for(const char *allowed : ALLOWED) {
    if(strcmp(field, allowed) == 0) return true;
  }
  return false;
}

bool peer_auth_key_valid(const std::string &key) {
  return key.empty() ||
         (key.size() >= LOADSHARING_KEY_MIN_LEN && key.size() <= LOADSHARING_KEY_MAX_LEN);
}

void PeerNonceStore::add(const std::string &nonce, uint32_t now) {
  _slots[_next].nonce = nonce;
  _slots[_next].issued = now;
  _next = (_next + 1) % LOADSHARING_NONCE_SLOTS;
}

bool PeerNonceStore::consume(const std::string &nonce, uint32_t now) {
  if(nonce.empty()) return false;
  for(Slot &slot : _slots) {
    if(slot.nonce.empty() || !auth_constant_time_equals(slot.nonce, nonce)) continue;
    bool fresh = (now - slot.issued) < LOADSHARING_NONCE_TTL_MS;
    slot.nonce.clear();
    return fresh;
  }
  return false;
}
