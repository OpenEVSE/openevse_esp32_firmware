#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <limits>

#include "certificate_id.h"

TEST_CASE("certificate IDs use full-width uppercase hexadecimal")
{
  CHECK(certificate_id_hex(0) == "0");
  CHECK(certificate_id_hex(0x0123456789ABCDEFULL) == "123456789ABCDEF");
  CHECK(certificate_id_hex(0x7FFFFFFFFFFFFFFFULL) == "7FFFFFFFFFFFFFFF");
  CHECK(certificate_id_hex(0x8000000000000000ULL) == "8000000000000000");
  CHECK(certificate_id_hex(0xFEDCBA9876543210ULL) == "FEDCBA9876543210");
  CHECK(certificate_id_hex(std::numeric_limits<uint64_t>::max()) == "FFFFFFFFFFFFFFFF");
}
