#ifndef OEVSE_HMAC_SHA256_H
#define OEVSE_HMAC_SHA256_H
#include <string>
#include <cstdint>
#include <cstddef>
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len, uint8_t out[32]);
std::string hmac_sha256_hex(const std::string &key, const std::string &msg);
#endif /* OEVSE_HMAC_SHA256_H */
