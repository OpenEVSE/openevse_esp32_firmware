#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <limits>

#include "certificate_id.h"

TEST_CASE("certificate IDs are uppercase hexadecimal across uint64 boundaries")
{
  CHECK(certificate_id_hex(0) == "0");
  CHECK(certificate_id_hex(0x71FBCCCFAC416709ULL) == "71FBCCCFAC416709");
  CHECK(certificate_id_hex(0x8000000000000000ULL) == "8000000000000000");
  CHECK(certificate_id_hex(0xB8F4009CB058E813ULL) == "B8F4009CB058E813");
  CHECK(certificate_id_hex(std::numeric_limits<uint64_t>::max()) == "FFFFFFFFFFFFFFFF");
}
