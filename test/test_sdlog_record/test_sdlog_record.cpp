#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "sdlog_record.h"

#include <string.h>

static SdlogRecord sample(uint32_t seq = 42, uint32_t ts = 1718459130)
{
  SdlogRecord r;
  r.seq = seq;
  r.timestamp = ts;
  const int16_t cols[SDLOG_RECORD_COLS] = { 160, 240, 384, 65, -25, -1, 32 };
  memcpy(r.cols, cols, sizeof(cols));
  return r;
}

TEST_CASE("a record is exactly 32 bytes and tiles a sector")
{
  CHECK(SDLOG_RECORD_BYTES == 32);
  CHECK(SDLOG_RECS_PER_SECTOR == 16);
  // The property the format exists for: no record straddles a sector boundary,
  // so a torn sector cannot desynchronise the scan.
  CHECK(SDLOG_SECTOR_BYTES % SDLOG_RECORD_BYTES == 0);
}

TEST_CASE("round-trips every field")
{
  uint8_t raw[SDLOG_RECORD_BYTES];
  SdlogRecord in = sample();
  sdlog_encode(in, raw);

  SdlogRecord out;
  REQUIRE(sdlog_decode(raw, out));
  CHECK(out.seq == in.seq);
  CHECK(out.timestamp == in.timestamp);
  for(int i = 0; i < SDLOG_RECORD_COLS; i++) {
    CHECK(out.cols[i] == in.cols[i]);
  }
}

TEST_CASE("preserves negative columns")
{
  // SoC uses -1 as "no reading" and temperature goes below zero; a sign lost in
  // the int16<->uint16 round trip would read back as ~65535.
  SdlogRecord in = sample();
  in.cols[5] = -1;
  in.cols[4] = -400;
  in.cols[0] = -32768;

  uint8_t raw[SDLOG_RECORD_BYTES];
  sdlog_encode(in, raw);

  SdlogRecord out;
  REQUIRE(sdlog_decode(raw, out));
  CHECK(out.cols[5] == -1);
  CHECK(out.cols[4] == -400);
  CHECK(out.cols[0] == -32768);
}

TEST_CASE("rejects a single flipped bit anywhere in the record")
{
  // This is the whole point: a torn or degraded write must not read back as
  // plausible data. Every payload bit must be covered by the CRC.
  SdlogRecord in = sample();
  uint8_t good[SDLOG_RECORD_BYTES];
  sdlog_encode(in, good);

  for(int byte = 0; byte < SDLOG_RECORD_BYTES; byte++)
  {
    for(int bit = 0; bit < 8; bit++)
    {
      uint8_t raw[SDLOG_RECORD_BYTES];
      memcpy(raw, good, sizeof(raw));
      raw[byte] ^= (uint8_t)(1 << bit);

      SdlogRecord out;
      CHECK_FALSE_MESSAGE(sdlog_decode(raw, out),
                          "accepted a record with bit ", bit, " of byte ", byte, " flipped");
    }
  }
}

TEST_CASE("rejects a half-written record")
{
  // The realistic tear: the first part of the record made it to the card and the
  // rest is whatever the sector held before.
  SdlogRecord in = sample();
  uint8_t raw[SDLOG_RECORD_BYTES];
  sdlog_encode(in, raw);

  for(int cut = 1; cut < SDLOG_RECORD_BYTES; cut++)
  {
    uint8_t torn[SDLOG_RECORD_BYTES];
    memcpy(torn, raw, sizeof(torn));
    memset(torn + cut, 0xFF, SDLOG_RECORD_BYTES - cut);

    SdlogRecord out;
    CHECK_FALSE_MESSAGE(sdlog_decode(torn, out), "accepted a record torn at byte ", cut);
  }
}

TEST_CASE("rejects an unwritten slot")
{
  uint8_t blank[SDLOG_RECORD_BYTES];
  SdlogRecord out;

  memset(blank, 0x00, sizeof(blank));
  CHECK(sdlog_is_blank(blank));
  CHECK_FALSE(sdlog_decode(blank, out));

  memset(blank, 0xFF, sizeof(blank));
  CHECK(sdlog_is_blank(blank));
  CHECK_FALSE(sdlog_decode(blank, out));
}

TEST_CASE("a real record is not mistaken for a blank slot")
{
  // Including the all-zero payload case, which is what an idle sample looks like.
  SdlogRecord in = sample(0, 0);
  memset(in.cols, 0, sizeof(in.cols));

  uint8_t raw[SDLOG_RECORD_BYTES];
  sdlog_encode(in, raw);
  CHECK_FALSE(sdlog_is_blank(raw));

  SdlogRecord out;
  CHECK(sdlog_decode(raw, out));
}

TEST_CASE("magic is checked, not just the CRC")
{
  // A slot holding some other structure with a coincidentally valid CRC should
  // still be refused; the magic is the cheap first gate.
  SdlogRecord in = sample();
  uint8_t raw[SDLOG_RECORD_BYTES];
  sdlog_encode(in, raw);
  raw[0] ^= 0xFF;

  SdlogRecord out;
  CHECK_FALSE(sdlog_decode(raw, out));
}

TEST_CASE("sequence comparison survives the uint32 wrap")
{
  CHECK(sdlog_seq_newer(2, 1));
  CHECK_FALSE(sdlog_seq_newer(1, 2));
  CHECK_FALSE(sdlog_seq_newer(1, 1));

  // The one comparison a plain a > b gets wrong.
  CHECK(sdlog_seq_newer(0, 0xFFFFFFFFUL));
  CHECK_FALSE(sdlog_seq_newer(0xFFFFFFFFUL, 0));
}

TEST_CASE("crc32 matches the known IEEE check value")
{
  // "123456789" -> 0xCBF43926 is the standard CRC-32/ISO-HDLC check vector. If
  // this drifts, cards written by an earlier build become unreadable.
  const char *check = "123456789";
  CHECK(sdlog_crc32((const uint8_t *)check, 9) == 0xCBF43926UL);
}

TEST_CASE("the encoding is stable")
{
  // Guards the on-card layout against an innocent-looking refactor. A card
  // written by an older firmware has to stay readable.
  SdlogRecord in = sample(0x01020304, 0x11223344);
  for(int i = 0; i < SDLOG_RECORD_COLS; i++) {
    in.cols[i] = (int16_t)(i + 1);
  }

  uint8_t raw[SDLOG_RECORD_BYTES];
  sdlog_encode(in, raw);

  const uint8_t expect_head[12] = {
    0x4F, 0x45, 0x56, 0x53,   // magic, "OEVS"
    0x04, 0x03, 0x02, 0x01,   // seq, little-endian
    0x44, 0x33, 0x22, 0x11,   // timestamp, little-endian
  };
  CHECK(memcmp(raw, expect_head, sizeof(expect_head)) == 0);

  CHECK(raw[12] == 0x01);
  CHECK(raw[13] == 0x00);
  CHECK(raw[26] == 0x00);   // reserved
  CHECK(raw[27] == 0x00);
}
