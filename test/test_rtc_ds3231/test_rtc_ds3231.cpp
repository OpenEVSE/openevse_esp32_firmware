#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "rtc_ds3231_regs.h"

#include <string.h>

// 2024-06-15 13:45:30 UTC == 1718459130
static const time_t KNOWN = 1718459130;

static void regs_for_known(uint8_t raw[DS3231_TIME_BYTES])
{
  raw[0] = 0x30;  // 30 s
  raw[1] = 0x45;  // 45 min
  raw[2] = 0x13;  // 13 h, 24-hour mode
  raw[3] = 0x07;  // Saturday
  raw[4] = 0x15;  // 15th
  raw[5] = 0x06;  // June, century 0
  raw[6] = 0x24;  // 2024
}

TEST_CASE("decodes a known timestamp")
{
  uint8_t raw[DS3231_TIME_BYTES];
  regs_for_known(raw);

  time_t out = 0;
  REQUIRE(ds3231_decode(raw, out));
  CHECK(out == KNOWN);
}

TEST_CASE("encode round-trips through decode")
{
  // A spread including a leap day, both century boundaries of the BCD year, and
  // midnight/23:59:59 edges.
  const time_t samples[] = {
    1700000000,  // the validity floor itself
    1718459130,  // the known value above
    1709164800,  // 2024-02-29 00:00:00, leap day
    1735689599,  // 2024-12-31 23:59:59
    1767225600,  // 2026-01-01 00:00:00
    4102444800,  // 2100-01-01, exercises the century bit
  };

  for(size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
  {
    uint8_t raw[DS3231_TIME_BYTES];
    REQUIRE_MESSAGE(ds3231_encode(samples[i], raw), "encode failed at index ", i);

    time_t back = 0;
    REQUIRE_MESSAGE(ds3231_decode(raw, back), "decode failed at index ", i);
    CHECK(back == samples[i]);
  }
}

TEST_CASE("sets the century bit past 2099 and not before")
{
  uint8_t raw[DS3231_TIME_BYTES];

  REQUIRE(ds3231_encode(1718459130, raw));   // 2024
  CHECK((raw[5] & 0x80) == 0);

  REQUIRE(ds3231_encode(4102444800, raw));   // 2100
  CHECK((raw[5] & 0x80) != 0);
}

TEST_CASE("writes 24-hour mode")
{
  uint8_t raw[DS3231_TIME_BYTES];
  REQUIRE(ds3231_encode(KNOWN, raw));
  CHECK((raw[2] & 0x40) == 0);  // bit 6 clear
}

TEST_CASE("reads a device left in 12-hour mode")
{
  // Same instant as KNOWN, expressed as 1:45:30 PM with the 12-hour bit set.
  uint8_t raw[DS3231_TIME_BYTES];
  regs_for_known(raw);
  raw[2] = 0x40 | 0x20 | 0x01;   // 12-hour, PM, 1 o'clock

  time_t out = 0;
  REQUIRE(ds3231_decode(raw, out));
  CHECK(out == KNOWN);
}

TEST_CASE("12-hour midnight and noon are not off by twelve")
{
  uint8_t raw[DS3231_TIME_BYTES];
  regs_for_known(raw);

  // 12:00:00 AM is hour 0, not hour 12.
  raw[0] = 0x00; raw[1] = 0x00;
  raw[2] = 0x40 | 0x12;          // 12-hour, AM, 12 o'clock
  time_t midnight = 0;
  REQUIRE(ds3231_decode(raw, midnight));

  raw[2] = 0x40 | 0x20 | 0x12;   // 12-hour, PM, 12 o'clock
  time_t noon = 0;
  REQUIRE(ds3231_decode(raw, noon));

  CHECK(noon - midnight == 12 * 3600);
}

TEST_CASE("rejects unpowered reads rather than inventing a date")
{
  // An absent or half-written device reads back 0xFF. 0xFF must not become 165.
  uint8_t raw[DS3231_TIME_BYTES];
  memset(raw, 0xFF, sizeof(raw));

  time_t out = 0;
  CHECK_FALSE(ds3231_decode(raw, out));
}

TEST_CASE("rejects malformed BCD in any field")
{
  for(int field = 0; field < DS3231_TIME_BYTES; field++)
  {
    if(field == 3) { continue; }   // day-of-week is not decoded

    uint8_t raw[DS3231_TIME_BYTES];
    regs_for_known(raw);
    raw[field] = 0x0A;             // low nibble = 10, not valid BCD

    time_t out = 0;
    CHECK_FALSE_MESSAGE(ds3231_decode(raw, out), "accepted bad BCD in field ", field);
  }
}

TEST_CASE("rejects a date that does not exist")
{
  // 31st of February. timegm() would silently normalise this to the 3rd of March.
  uint8_t raw[DS3231_TIME_BYTES];
  regs_for_known(raw);
  raw[4] = 0x31;   // 31st
  raw[5] = 0x02;   // February

  time_t out = 0;
  CHECK_FALSE(ds3231_decode(raw, out));
}

TEST_CASE("rejects out-of-range fields")
{
  struct { int field; uint8_t value; const char *what; } cases[] = {
    {0, 0x60, "60 seconds"},
    {1, 0x60, "60 minutes"},
    {2, 0x24, "hour 24"},
    {4, 0x00, "day 0"},
    {5, 0x13, "month 13"},
    {5, 0x00, "month 0"},
  };

  for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    uint8_t raw[DS3231_TIME_BYTES];
    regs_for_known(raw);
    raw[cases[i].field] = cases[i].value;

    time_t out = 0;
    CHECK_FALSE_MESSAGE(ds3231_decode(raw, out), "accepted ", cases[i].what);
  }
}

TEST_CASE("rejects a time before the validity floor")
{
  // A dead cell typically presents as 2000-01-01 00:00:00. Seeding the system
  // clock with that is exactly the corruption the floor exists to prevent.
  uint8_t raw[DS3231_TIME_BYTES] = { 0x00, 0x00, 0x00, 0x07, 0x01, 0x01, 0x00 };

  time_t out = 0;
  CHECK_FALSE(ds3231_decode(raw, out));

  CHECK_FALSE(ds3231_time_plausible(0));
  CHECK_FALSE(ds3231_time_plausible(946684800));            // 2000-01-01
  CHECK_FALSE(ds3231_time_plausible(RTC_TIME_VALID_FLOOR - 1));
  CHECK(ds3231_time_plausible(RTC_TIME_VALID_FLOOR));
}

TEST_CASE("refuses to encode a time the device cannot hold")
{
  uint8_t raw[DS3231_TIME_BYTES];
  CHECK_FALSE(ds3231_encode(0, raw));            // 1970
  CHECK_FALSE(ds3231_encode(946684799, raw));    // 1999-12-31 23:59:59
  CHECK(ds3231_encode(946684800, raw));          // 2000-01-01, first holdable
}

TEST_CASE("ignores the unused high bits the device sets")
{
  // Bit 7 of seconds and the century bit are not part of the value; the alarm
  // mask bits live in different registers but stray bits should not derail us.
  uint8_t raw[DS3231_TIME_BYTES];
  regs_for_known(raw);
  raw[0] |= 0x80;

  time_t out = 0;
  REQUIRE(ds3231_decode(raw, out));
  CHECK(out == KNOWN);
}

TEST_CASE("writes a coherent day-of-week")
{
  // DS3231 day-of-week is 1..7. We only ever use the alarm-free path, but a
  // human reading the registers should not see nonsense. 2024-06-15 was a
  // Saturday; with Sunday==1 that is 7.
  uint8_t raw[DS3231_TIME_BYTES];
  REQUIRE(ds3231_encode(KNOWN, raw));
  CHECK(raw[3] == 7);

  // And it advances by one per day, wrapping 7 -> 1.
  REQUIRE(ds3231_encode(KNOWN + 86400, raw));
  CHECK(raw[3] == 1);

  for(int day = 0; day < 14; day++)
  {
    REQUIRE(ds3231_encode(KNOWN + (time_t)day * 86400, raw));
    CHECK(raw[3] >= 1);
    CHECK(raw[3] <= 7);
  }
}
