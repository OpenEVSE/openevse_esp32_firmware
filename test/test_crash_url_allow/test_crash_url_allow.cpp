// Host-side tests for the crash-broker URL allowlist (ota_url_allow.cpp).
// A core dump is a RAM image carrying the WiFi PSK and every stored token, so
// this list is exact-match and single-entry -- unlike the OTA list next to it,
// which has to admit a CDN.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ota_url_allow.h"
#include "crash_host.h"

TEST_CASE("the compiled-in broker host is allowed") {
  CHECK(crash_url_host_allowed("https://" CRASH_BROKER_HOST "/v1/reports"));
  CHECK(crash_url_host_allowed("HTTPS://" CRASH_BROKER_HOST "/v1/reports"));
  CHECK(crash_url_host_allowed("https://" CRASH_BROKER_HOST ":443/v1/reports"));
}

TEST_CASE("nothing else is, including neighbours of the real host") {
  // Subdomains are NOT admitted: the OTA list accepts *.githubusercontent.com
  // because a CDN needs it, and copying that here would mean anyone who can
  // create a record under the zone can receive memory images.
  CHECK_FALSE(crash_url_host_allowed("https://evil.crash.openevse.com/v1/reports"));
  CHECK_FALSE(crash_url_host_allowed("https://openevse.com/v1/reports"));
  CHECK_FALSE(crash_url_host_allowed("https://crash.openevse.com.evil.example/v1"));
  CHECK_FALSE(crash_url_host_allowed("https://github.com/v1/reports"));
  // S3 is explicitly not allowed -- this is why the raw PUT goes to the broker
  // rather than to a presigned URL (deviation D1).
  CHECK_FALSE(crash_url_host_allowed("https://bkt.s3.us-east-2.amazonaws.com/x?X-Amz-Signature=y"));
}

TEST_CASE("plain http is refused") {
  CHECK_FALSE(crash_url_host_allowed("http://" CRASH_BROKER_HOST "/v1/reports"));
  CHECK_FALSE(crash_url_host_allowed(CRASH_BROKER_HOST "/v1/reports"));
}

TEST_CASE("the userinfo bypass is refused here too") {
  CHECK_FALSE(crash_url_host_allowed("https://" CRASH_BROKER_HOST ":1@evil.example/v1"));
  CHECK_FALSE(crash_url_host_allowed("https://" CRASH_BROKER_HOST "?@evil.example/v1"));
  CHECK_FALSE(crash_url_host_allowed("https://" CRASH_BROKER_HOST "\\@evil.example/v1"));
}

TEST_CASE("nothing at all is refused rather than crashing") {
  CHECK_FALSE(crash_url_host_allowed(NULL));
  CHECK_FALSE(crash_url_host_allowed(""));
}

TEST_CASE("the OTA list is unchanged by the refactor") {
  CHECK(ota_url_host_allowed("https://github.com/OpenEVSE/x/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://" CRASH_BROKER_HOST "/fw.bin"));
}
