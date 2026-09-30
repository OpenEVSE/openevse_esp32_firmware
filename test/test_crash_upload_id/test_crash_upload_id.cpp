// Host-side tests for the pure parts of crash reporting: the reporter
// identity the browser stores on the charger, and whether a dump came from
// the running build.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "crash_report_id.h"

#include <string>

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

// ---------------------------------------------------------------------------
// What a "Delete my reports" click does. Withdrawing consent should be as easy
// as giving it, so a charger that cannot do it right now (no network, or the
// heap too fragmented for a TLS session, which is normal straight after an
// upload on a no-PSRAM board) defers to the next boot instead of refusing.
// ---------------------------------------------------------------------------

