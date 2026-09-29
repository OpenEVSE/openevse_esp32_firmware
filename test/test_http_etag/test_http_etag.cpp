// Host-side tests for the conditional-request helpers (http_etag.cpp).
//
// The bug these exist to prevent: the handler emitted a bare, unquoted entity
// tag and compared If-None-Match with an exact string match. Browsers hand the
// tag back quoted, the compare never fired, and every page load re-sent the
// full UI bundle while looking, from the device side, like a working cache.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "http_etag.h"

#include <string.h>

static bool match(const char *header, const char *etag) {
  return http_etag_matches(header, strlen(header), etag);
}

static const char *HASH = "21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0";

TEST_CASE("the quoted form a browser sends matches the stored bare tag") {
  // This is the exact case that was broken on hardware.
  CHECK(match("\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\"", HASH));
}

TEST_CASE("bare, weak and weak-quoted forms all match") {
  CHECK(match(HASH, HASH));
  CHECK(match("W/\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\"", HASH));
  CHECK(match("W/21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0", HASH));
  // ...and a stored tag that already carries quotes still matches both ways.
  CHECK(match(HASH, "\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\""));
}

TEST_CASE("a list matches on any member, in any position") {
  CHECK(match("\"aaa\", \"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\"", HASH));
  CHECK(match("\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\", \"bbb\"", HASH));
  CHECK(match("\"aaa\",\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\",\"bbb\"", HASH));
  // Whitespace around the separators is optional and may be a tab.
  CHECK(match("\t\"aaa\" ,\t\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\" ", HASH));
  // Empty members are skipped rather than matching something.
  CHECK(match(",,\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\",,", HASH));
}

TEST_CASE("* matches anything") {
  CHECK(match("*", HASH));
  CHECK(match(" * ", HASH));
  CHECK(match("\"aaa\", *", HASH));
}

TEST_CASE("non-matching tags do not match") {
  CHECK_FALSE(match("\"aaa\"", HASH));
  CHECK_FALSE(match("\"\"", HASH));
  // A prefix of the real tag must not match: length is compared, not just the
  // leading bytes.
  CHECK_FALSE(match("\"21293ded\"", HASH));
  // Nor may a superset.
  CHECK_FALSE(match("\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0f\"", HASH));
  // '*' only counts as a wildcard when it is the whole tag.
  CHECK_FALSE(match("\"*\"", HASH));
  CHECK_FALSE(match("**", HASH));
}

TEST_CASE("empty and null inputs are a miss, not a match") {
  CHECK_FALSE(http_etag_matches(NULL, 0, HASH));
  CHECK_FALSE(http_etag_matches("", 0, HASH));
  CHECK_FALSE(http_etag_matches(HASH, strlen(HASH), NULL));
  CHECK_FALSE(http_etag_matches(HASH, strlen(HASH), ""));
  CHECK_FALSE(http_etag_matches(HASH, strlen(HASH), "\"\""));
  // A miss here is safe (a full 200); a false match would serve a stale body.
  CHECK_FALSE(match("   ", HASH));
}

TEST_CASE("the header need not be NUL-terminated") {
  // Mongoose hands us a pointer/length into the request buffer, so the byte
  // after the value is whatever followed it on the wire.
  const char *raw = "\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\"\r\nHost: x";
  CHECK(http_etag_matches(raw, 66, HASH));
  // Truncating inside the tag must not match.
  CHECK_FALSE(http_etag_matches(raw, 20, HASH));
}

TEST_CASE("quoting adds quotes exactly once") {
  char out[HTTP_ETAG_QUOTED_MAX];

  CHECK(http_etag_quote(HASH, out, sizeof(out)));
  CHECK(0 == strcmp(out, "\"21293ded49db24e51fd0ab812561afe63b20618aadc7f22343c46173d97aeac0\""));

  CHECK(http_etag_quote("\"abc\"", out, sizeof(out)));
  CHECK(0 == strcmp(out, "\"abc\""));

  CHECK(http_etag_quote("W/\"abc\"", out, sizeof(out)));
  CHECK(0 == strcmp(out, "\"abc\""));
}

TEST_CASE("quoting refuses rather than truncating") {
  char out[8];
  CHECK_FALSE(http_etag_quote(HASH, out, sizeof(out)));
  CHECK(0 == strlen(out));   // and leaves nothing half-written behind

  CHECK_FALSE(http_etag_quote(NULL, out, sizeof(out)));
  CHECK(0 == strlen(out));
  CHECK_FALSE(http_etag_quote("", out, sizeof(out)));

  // Exactly-fits is allowed: 3 chars + 2 quotes + NUL.
  char snug[6];
  CHECK(http_etag_quote("abc", snug, sizeof(snug)));
  CHECK(0 == strcmp(snug, "\"abc\""));
}

TEST_CASE("a tag that round-trips through quoting still matches") {
  char out[HTTP_ETAG_QUOTED_MAX];
  REQUIRE(http_etag_quote(HASH, out, sizeof(out)));
  // What we send is what the browser gives back, so this is the whole contract.
  CHECK(match(out, HASH));
}
