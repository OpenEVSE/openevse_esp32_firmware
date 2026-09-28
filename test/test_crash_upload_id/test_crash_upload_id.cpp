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
