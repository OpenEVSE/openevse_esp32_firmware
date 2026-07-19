#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "crypto/hmac_sha256.h"

TEST_CASE("HMAC-SHA256 RFC 4231 test case 2") {
  // key="Jefe", data="what do ya want for nothing?"
  std::string mac = hmac_sha256_hex("Jefe", "what do ya want for nothing?");
  CHECK(mac == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("HMAC-SHA256 differs on key change") {
  CHECK(hmac_sha256_hex("k1", "msg") != hmac_sha256_hex("k2", "msg"));
}
