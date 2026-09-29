// Host-side tests for the one piece of the uploader that is pure: deciding
// whether the id a broker handed back may be built into a request path.
// Review Focus 1 -- the broker's reply is attacker-controlled once the broker
// is, and this id is the only part of it the device acts on.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "crash_report_id.h"

#include <string>

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

// ---------------------------------------------------------------------------
// What the metadata step promises to PUT afterwards. A shipped build sends the
// decoded summary only: the raw image is a copy of RAM, and no redaction can
// say what that copy holds.
// ---------------------------------------------------------------------------

TEST_CASE("a default build declares no raw dump") {
  CHECK(crash_declared_raw_bytes(65536) == 0);
  CHECK(crash_declared_raw_bytes(0) == 0);
}

// ---------------------------------------------------------------------------
// The reporter identity: a random id sent in place of the chip id, and a
// random delete key that never leaves the device except to erase.
// ---------------------------------------------------------------------------

static const char *RID = "0123456789abcdef0123456789abcdef";
static const char *KEY = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

TEST_CASE("hex encoding is lower-case and exact") {
  const uint8_t b[] = { 0x00, 0x0f, 0xa5, 0xff };
  char out[9];
  crash_hex(b, sizeof(b), out);
  CHECK(std::string(out) == "000fa5ff");
}

TEST_CASE("reporter ids and delete keys are fixed-length lower-case hex") {
  CHECK(crash_reporter_id_valid(RID));
  CHECK_FALSE(crash_reporter_id_valid("0123456789ABCDEF0123456789abcdef"));
  CHECK_FALSE(crash_reporter_id_valid("0123456789abcdef0123456789abcde"));
  CHECK_FALSE(crash_reporter_id_valid("0123456789abcdef0123456789abcdef0"));
  CHECK_FALSE(crash_reporter_id_valid(NULL));
  CHECK(crash_delete_key_valid(KEY));
  CHECK_FALSE(crash_delete_key_valid(RID));
  CHECK_FALSE(crash_delete_key_valid(NULL));
}

TEST_CASE("the delete key hash is the SHA-256 of the key's hex text") {
  // What the broker computes from the key the device later presents
  // (hashlib.sha256(key.encode()).hexdigest()).
  char out[65];
  crash_delete_key_hash(KEY, out);
  CHECK(std::string(out) == "2a8abfa8cb9906290437854193ca6bca41d4d4e26d1d454bd66a35158095e737");
}

TEST_CASE("the identity file round-trips, and anything else is rejected") {
  char text[CRASH_IDENTITY_LEN];
  crash_identity_format(RID, KEY, text, sizeof(text));
  char rid[33], key[65];
  REQUIRE(crash_identity_parse(text, rid, key));
  CHECK(std::string(rid) == RID);
  CHECK(std::string(key) == KEY);

  CHECK_FALSE(crash_identity_parse("", rid, key));
  CHECK_FALSE(crash_identity_parse(NULL, rid, key));
  CHECK_FALSE(crash_identity_parse(RID, rid, key));
  std::string swapped = std::string(KEY) + "\n" + RID + "\n";
  CHECK_FALSE(crash_identity_parse(swapped.c_str(), rid, key));
  std::string trailing = std::string(text) + "junk";
  CHECK_FALSE(crash_identity_parse(trailing.c_str(), rid, key));
}
