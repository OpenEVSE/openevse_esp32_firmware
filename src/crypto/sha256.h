/*
 * SHA-256 — public-domain implementation by Brad Conte
 * (https://github.com/B-Con/crypto-algorithms)
 *
 * This code is released into the public domain free of any restrictions.
 */
#ifndef OEVSE_SHA256_H
#define OEVSE_SHA256_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
#ifdef __cplusplus
}
#endif
#endif /* OEVSE_SHA256_H */
