// Host-side tests for the OTA firmware-fetch URL allowlist (ota_url_allow.cpp).
// Covers the userinfo/port parsing that a naive host-extraction gets wrong,
// including the "github.com:x@evil.example" bypass.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ota_url_allow.h"

TEST_CASE("legitimate GitHub release hosts are allowed") {
  CHECK(ota_url_host_allowed("https://github.com/OpenEVSE/openevse_esp32_firmware/releases/download/latest/openevse_wifi_v1_16mb.bin"));
  CHECK(ota_url_host_allowed("https://github.com/anything"));
  CHECK(ota_url_host_allowed("https://raw.githubusercontent.com/OpenEVSE/x/main/fw.bin"));
  CHECK(ota_url_host_allowed("https://objects.githubusercontent.com/github-production-release-asset/x"));
  CHECK(ota_url_host_allowed("https://release-assets.githubusercontent.com/x"));
  // explicit port on a real allowed host
  CHECK(ota_url_host_allowed("https://github.com:443/OpenEVSE/x/fw.bin"));
  // scheme is case-insensitive
  CHECK(ota_url_host_allowed("HTTPS://github.com/x"));
}

TEST_CASE("userinfo / port bypass attempts are rejected") {
  // The key bypass: ':' inside userinfo before '@' must not truncate the host.
  CHECK_FALSE(ota_url_host_allowed("https://github.com:1@evil.example/firmware.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://github.com:anything@evil.example/firmware.bin"));
  // '?' before '@' must not truncate the host either: Mongoose's userinfo scan
  // does not treat '?' as an authority delimiter, so it connects to evil.example.
  CHECK_FALSE(ota_url_host_allowed("https://github.com?@evil.example/firmware.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://github.com?x@evil.example/firmware.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://github.com?@192.0.2.66/fw.bin"));
  // '#' before '@' is caught by the delimiter reject too.
  CHECK_FALSE(ota_url_host_allowed("https://github.com#@evil.example/fw.bin"));
  // Plain userinfo form.
  CHECK_FALSE(ota_url_host_allowed("https://github.com@evil.example/firmware.bin"));
  // Password-style userinfo.
  CHECK_FALSE(ota_url_host_allowed("https://user:github.com@evil.example/fw.bin"));
  // Allowed host only in the path, not the authority.
  CHECK_FALSE(ota_url_host_allowed("https://evil.example/github.com"));
  CHECK_FALSE(ota_url_host_allowed("https://evil.example/?x=github.com"));
}

TEST_CASE("look-alike and wrong-scheme hosts are rejected") {
  CHECK_FALSE(ota_url_host_allowed("https://notgithub.com/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://github.com.evil.example/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://githubusercontent.com.evil.example/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://evilgithub.com/fw.bin"));
  // must be https
  CHECK_FALSE(ota_url_host_allowed("http://github.com/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("ftp://github.com/fw.bin"));
  // malformed / empty
  CHECK_FALSE(ota_url_host_allowed("github.com/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://"));
  CHECK_FALSE(ota_url_host_allowed(""));
  CHECK_FALSE(ota_url_host_allowed(nullptr));
}

TEST_CASE("exact apex and subdomain suffix matching") {
  CHECK(ota_url_host_allowed("https://github.com"));
  CHECK(ota_url_host_allowed("https://githubusercontent.com/x"));
  // ".githubusercontent.com" suffix (subdomain) allowed; bare different host not
  CHECK(ota_url_host_allowed("https://a.b.githubusercontent.com/x"));
  CHECK_FALSE(ota_url_host_allowed("https://xgithubusercontent.com/x"));
}
