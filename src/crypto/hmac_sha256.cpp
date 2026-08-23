#include "crypto/hmac_sha256.h"
#include "crypto/sha256.h"
#include <cstring>

static const size_t BLOCK = 64;

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len, uint8_t out[32]) {
  uint8_t k[BLOCK]; std::memset(k, 0, BLOCK);
  if(key_len > BLOCK) { sha256(key, key_len, k); }
  else { std::memcpy(k, key, key_len); }

  uint8_t ipad[BLOCK], opad[BLOCK];
  for(size_t i = 0; i < BLOCK; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

  // inner = SHA256(ipad || msg)
  uint8_t inner[32];
  { std::string buf; buf.assign((char*)ipad, BLOCK); buf.append((char*)msg, msg_len);
    sha256((const uint8_t*)buf.data(), buf.size(), inner); }
  // out = SHA256(opad || inner)
  { std::string buf; buf.assign((char*)opad, BLOCK); buf.append((char*)inner, 32);
    sha256((const uint8_t*)buf.data(), buf.size(), out); }
}

std::string hmac_sha256_hex(const std::string &key, const std::string &msg) {
  uint8_t mac[32];
  hmac_sha256((const uint8_t*)key.data(), key.size(),
              (const uint8_t*)msg.data(), msg.size(), mac);
  static const char *H = "0123456789abcdef";
  std::string out; out.reserve(64);
  for(int i = 0; i < 32; i++) { out += H[mac[i] >> 4]; out += H[mac[i] & 0xf]; }
  return out;
}
