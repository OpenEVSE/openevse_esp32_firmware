// Host-side tests for the one piece of the uploader that is pure: deciding
// whether the id a broker handed back may be built into a request path.
// Review Focus 1 -- the broker's reply is attacker-controlled once the broker
// is, and this id is the only part of it the device acts on.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "crash_report_id.h"

TEST_CASE("a real report id is accepted") {
  CHECK(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c3301"));
  CHECK(crash_report_id_valid("00000000-0000-0000-0000-000000000000"));
}

TEST_CASE("anything that could escape the path is refused") {
  CHECK_FALSE(crash_report_id_valid("../../../elf/firmware.elf"));
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c3301/.."));
  CHECK_FALSE(crash_report_id_valid("3f2504e0 4f89 41d3 9a0c 0305e82c3301"));
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c33\r\nX: y"));
  CHECK_FALSE(crash_report_id_valid("?X-Amz-Signature=abc"));
}

TEST_CASE("the wrong shape is refused, including near misses") {
  CHECK_FALSE(crash_report_id_valid(""));
  CHECK_FALSE(crash_report_id_valid(NULL));
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c330"));   // short
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c33011")); // long
  CHECK_FALSE(crash_report_id_valid("3F2504E0-4F89-41D3-9A0C-0305E82C3301"));  // upper
  CHECK_FALSE(crash_report_id_valid("3f2504e04f8941d39a0c0305e82c3301"));      // no dashes
}

// ---------------------------------------------------------------------------
// Which dump a deferred click consented to (spec section 8). The flag records
// the offered dump's identity; a different dump at the next boot -- a crash
// AFTER the click -- was never offered and must not be sent.
// ---------------------------------------------------------------------------

TEST_CASE("the token for a dump matches only that dump") {
  char tok[CRASH_DEFER_TOKEN_LEN];
  crash_defer_token(tok, 0x5633e718c3722dd1ULL, 26084);
  CHECK(crash_defer_token_matches(tok, 0x5633e718c3722dd1ULL, 26084));
  // The bench pair that broke a CRC32 identity: same size, different bytes.
  CHECK_FALSE(crash_defer_token_matches(tok, 0x33d7fd072a0b6813ULL, 26084));
  CHECK_FALSE(crash_defer_token_matches(tok, 0x5633e718c3722dd1ULL, 26088));
  // Both 32-bit halves count -- a token built from only one would collide here.
  CHECK_FALSE(crash_defer_token_matches(tok, 0x00000000c3722dd1ULL, 26084));
  CHECK_FALSE(crash_defer_token_matches(tok, 0x5633e71800000000ULL, 26084));
}

TEST_CASE("an unreadable or legacy flag consents to nothing") {
  // A flag written by an older build held "1". Treating that as consent for
  // whatever dump exists now would reopen exactly the gap this closes.
  CHECK_FALSE(crash_defer_token_matches("1", 0x5633e718c3722dd1ULL, 26084));
  CHECK_FALSE(crash_defer_token_matches("", 0x5633e718c3722dd1ULL, 26084));
  CHECK_FALSE(crash_defer_token_matches(NULL, 0x5633e718c3722dd1ULL, 26084));
  CHECK_FALSE(crash_defer_token_matches("5633e718c3722dd1:26084junk", 0x5633e718c3722dd1ULL, 26084));
  // A token written by the CRC32 version of this code consents to nothing.
  CHECK_FALSE(crash_defer_token_matches("2144df1c:26084", 0x2144df1cULL, 26084));
}

// ---------------------------------------------------------------------------
// Whether the stored dump came from the firmware that is running now. After an
// OTA it does not, and the running version would misfile the crash.
// ---------------------------------------------------------------------------

TEST_CASE("a dump from the running build is recognised as such") {
  CHECK(crash_dump_from_running_build("c043b880d", "c043b880d"));
  // The running hash may be read longer than the 9 characters the dump carries.
  CHECK(crash_dump_from_running_build("c043b880d", "c043b880d786aa7b"));
}

TEST_CASE("a dump from any other build, or an unknown one, is not") {
  CHECK_FALSE(crash_dump_from_running_build("c043b880d", "e29b390b9"));
  CHECK_FALSE(crash_dump_from_running_build("", "e29b390b9"));
  CHECK_FALSE(crash_dump_from_running_build(NULL, "e29b390b9"));
  CHECK_FALSE(crash_dump_from_running_build("c043b880d", NULL));
  // Too short to identify a build: prefer "unknown" to a guess.
  CHECK_FALSE(crash_dump_from_running_build("c04", "c043b880d"));
}
