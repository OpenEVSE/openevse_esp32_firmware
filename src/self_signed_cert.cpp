#include "self_signed_cert.h"

#if defined(CERT_VALIDATOR_OPENSSL)

bool generateSelfSignedCertificate(const String &commonName, const String &ipAddress, String &certPem, String &keyPem)
{
  (void)commonName;
  (void)ipAddress;
  (void)certPem;
  (void)keyPem;
  return false;
}

#else

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/version.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/oid.h>
#include <IPAddress.h>
#include <string.h>

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
#include <esp_random.h>
static int esp_rng_for_mbedtls(void *ctx, unsigned char *buf, size_t len)
{
  (void)ctx;
  esp_fill_random(buf, len);
  return 0;
}
#else
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/bignum.h>
#endif

static int addSubjectAltNames(mbedtls_x509write_cert &crt, const String &dnsName, const String &ipAddress)
{
  // SAN payload is one DNS name (hostname config is short) plus one IPv4
  // address, with ASN.1 tags/lengths. 160 bytes leaves ample headroom while
  // keeping this stack buffer small for ESP32.
  uint8_t buf[160];
  uint8_t *p = buf + sizeof(buf);
  size_t len = 0;
  int ret;

  IPAddress ip;
  if(ipAddress.length() > 0 && ip.fromString(ipAddress)) {
    uint8_t octets[4] = { ip[0], ip[1], ip[2], ip[3] };
    size_t entryLen = 0;
    if((ret = mbedtls_asn1_write_raw_buffer(&p, buf, octets, sizeof(octets))) < 0) return ret;
    entryLen += ret;
    if((ret = mbedtls_asn1_write_len(&p, buf, sizeof(octets))) < 0) return ret;
    entryLen += ret;
    if((ret = mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONTEXT_SPECIFIC | 7)) < 0) return ret;
    entryLen += ret;
    len += entryLen;
  }

  if(dnsName.length() > 0) {
    size_t entryLen = 0;
    if((ret = mbedtls_asn1_write_raw_buffer(&p, buf, (const uint8_t *)dnsName.c_str(), dnsName.length())) < 0) return ret;
    entryLen += ret;
    if((ret = mbedtls_asn1_write_len(&p, buf, dnsName.length())) < 0) return ret;
    entryLen += ret;
    if((ret = mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONTEXT_SPECIFIC | 2)) < 0) return ret;
    entryLen += ret;
    len += entryLen;
  }

  if(0 == len) return MBEDTLS_ERR_X509_BAD_INPUT_DATA;

  if((ret = mbedtls_asn1_write_len(&p, buf, len)) < 0) return ret;
  len += ret;
  if((ret = mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)) < 0) return ret;
  len += ret;

  return mbedtls_x509write_crt_set_extension(&crt, MBEDTLS_OID_SUBJECT_ALT_NAME,
    MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME), 0, p, len);
}

bool generateSelfSignedCertificate(const String &commonName, const String &ipAddress, String &certPem, String &keyPem)
{
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  #define RNG_FN esp_rng_for_mbedtls
  #define RNG_CTX nullptr
  bool rngOk = true;
#else
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  static const char *pers = "openevse_selfsigned";
  bool rngOk = (0 == mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                            (const unsigned char *)pers, strlen(pers)));
  #define RNG_FN mbedtls_ctr_drbg_random
  #define RNG_CTX (&ctr_drbg)
#endif

  mbedtls_pk_context key;
  mbedtls_pk_init(&key);

  bool ok = rngOk &&
    0 == mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) &&
    0 == mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), RNG_FN, RNG_CTX);

  if(ok) {
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);

    String dn = "CN=" + commonName;
    ok = 0 == mbedtls_x509write_crt_set_subject_name(&crt, dn.c_str()) &&
         0 == mbedtls_x509write_crt_set_issuer_name(&crt, dn.c_str()) &&
         0 == mbedtls_x509write_crt_set_validity(&crt, "20200101000000", "20460101000000");

    if(ok) {
      uint8_t serialBytes[8];
      RNG_FN(RNG_CTX, serialBytes, sizeof(serialBytes));
      serialBytes[0] &= 0x7F;

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
      ok = (0 == mbedtls_x509write_crt_set_serial_raw(&crt, serialBytes, sizeof(serialBytes)));
#else
      mbedtls_mpi serial;
      mbedtls_mpi_init(&serial);
      mbedtls_mpi_read_binary(&serial, serialBytes, sizeof(serialBytes));
      ok = (0 == mbedtls_x509write_crt_set_serial(&crt, &serial));
      mbedtls_mpi_free(&serial);
#endif
    }

    if(ok) {
      ok = (0 == addSubjectAltNames(crt, commonName, ipAddress));
    }

    if(ok) {
      uint8_t certBuf[2048];
      ok = (mbedtls_x509write_crt_pem(&crt, certBuf, sizeof(certBuf), RNG_FN, RNG_CTX) >= 0);
      if(ok) certPem = String((const char *)certBuf);
    }

    mbedtls_x509write_crt_free(&crt);
  }

  if(ok) {
    uint8_t keyBuf[1024];
    ok = (0 == mbedtls_pk_write_key_pem(&key, keyBuf, sizeof(keyBuf)));
    if(ok) keyPem = String((const char *)keyBuf);
  }

  mbedtls_pk_free(&key);
#if MBEDTLS_VERSION_NUMBER < 0x03000000
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
#endif

#undef RNG_FN
#undef RNG_CTX

  return ok;
}

#endif
