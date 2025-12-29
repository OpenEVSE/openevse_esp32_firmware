#pragma once

// Minimal host-build stub for a subset of mbedTLS PK API used by certificates.cpp.
// Used only for PlatformIO `native` builds.

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int dummy;
} mbedtls_pk_context;

static inline void mbedtls_pk_init(mbedtls_pk_context* /*ctx*/) {}

static inline int mbedtls_pk_parse_key(mbedtls_pk_context* /*ctx*/, const unsigned char* /*key*/, size_t /*keylen*/, const unsigned char* /*pwd*/, size_t /*pwdlen*/) {
  return 0; // success
}

#ifdef __cplusplus
}
#endif
