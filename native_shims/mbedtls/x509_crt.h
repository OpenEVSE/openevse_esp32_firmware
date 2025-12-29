#pragma once

// Minimal host-build stub for a subset of mbedTLS X.509 API used by certificates.cpp.
// Used only for PlatformIO `native` builds.

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  size_t len;
  unsigned char* p;
} mbedtls_x509_buf;

typedef struct mbedtls_x509_name {
  int dummy;
} mbedtls_x509_name;

typedef struct {
  mbedtls_x509_buf serial;
  mbedtls_x509_name issuer;
  mbedtls_x509_name subject;
} mbedtls_x509_crt;

static inline void mbedtls_x509_crt_init(mbedtls_x509_crt* crt) {
  if (!crt) return;
  crt->serial.len = 0;
  crt->serial.p = nullptr;
}

static inline void mbedtls_x509_crt_free(mbedtls_x509_crt* /*crt*/) {}

static inline int mbedtls_x509_crt_parse(mbedtls_x509_crt* /*crt*/, const unsigned char* /*buf*/, size_t /*buflen*/) {
  return 0; // success
}

static inline int mbedtls_x509_dn_gets(char* buf, size_t size, const mbedtls_x509_name* /*dn*/) {
  if (!buf || size == 0) return 0;
  buf[0] = '\0';
  return 0;
}

#ifdef __cplusplus
}
#endif
