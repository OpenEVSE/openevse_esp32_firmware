#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "sdlog_ring.h"

#include <string.h>
#include <vector>

// In-memory ring standing in for the card.
struct FakeRing {
  std::vector<uint8_t> data;
  uint32_t capacity;
  uint32_t reads = 0;
  bool fail_reads = false;

  explicit FakeRing(uint32_t cap) : data(cap * SDLOG_RECORD_BYTES, 0xFF), capacity(cap) {}

  void write(uint32_t seq)
  {
    SdlogRecord r;
    r.seq = seq;
    r.timestamp = 1700000000UL + seq * 60;
    for(int i = 0; i < SDLOG_RECORD_COLS; i++) { r.cols[i] = (int16_t)(seq + i); }
    sdlog_encode(r, &data[(seq % capacity) * SDLOG_RECORD_BYTES]);
  }

  // Fill as the logger would: seq 0..count-1, wrapping.
  void fill(uint32_t count)
  {
    for(uint32_t s = 0; s < count; s++) { write(s); }
  }

  void tear(uint32_t index)
  {
    // A torn write: front of the record landed, the rest is stale.
    memset(&data[index * SDLOG_RECORD_BYTES] + 8, 0xA5, SDLOG_RECORD_BYTES - 8);
  }

  void tear_sector(uint32_t first_index)
  {
    for(uint32_t i = 0; i < SDLOG_RECS_PER_SECTOR && first_index + i < capacity; i++) {
      tear(first_index + i);
    }
  }
};

static bool fake_read(void *ctx, uint32_t index, uint8_t out[SDLOG_RECORD_BYTES])
{
  FakeRing *r = (FakeRing *)ctx;
  if(r->fail_reads) { return false; }
  if(index >= r->capacity) { return false; }
  r->reads++;
  memcpy(out, &r->data[index * SDLOG_RECORD_BYTES], SDLOG_RECORD_BYTES);
  return true;
}

static SdlogRingScan scan_of(FakeRing &ring, bool expect_ok = true)
{
  SdlogRingScan out;
  bool ok = sdlog_ring_scan(fake_read, &ring, ring.capacity, out);
  REQUIRE(ok == expect_ok);
  return out;
}

TEST_CASE("an untouched ring starts at zero")
{
  FakeRing ring(256);
  SdlogRingScan s = scan_of(ring);
  CHECK_FALSE(s.any_records);
  CHECK(s.next_seq == 0);
  CHECK(s.next_index == 0);
}

TEST_CASE("finds the head of a partly filled ring")
{
  for(uint32_t written : { (uint32_t)1, (uint32_t)2, (uint32_t)17, (uint32_t)100, (uint32_t)255 })
  {
    FakeRing ring(256);
    ring.fill(written);

    SdlogRingScan s = scan_of(ring);
    CHECK_MESSAGE(s.any_records, "no records after writing ", written);
    CHECK_MESSAGE(s.newest_seq == written - 1, "wrong head after writing ", written);
    CHECK(s.next_seq == written);
    CHECK(s.next_index == written % 256);
  }
}

TEST_CASE("finds the head of an exactly full ring")
{
  FakeRing ring(256);
  ring.fill(256);

  SdlogRingScan s = scan_of(ring);
  CHECK(s.newest_seq == 255);
  CHECK(s.next_seq == 256);
  CHECK(s.next_index == 0);
}

TEST_CASE("finds the head after wrapping, at every rotation")
{
  const uint32_t cap = 64;
  for(uint32_t extra = 1; extra < cap; extra++)
  {
    FakeRing ring(cap);
    ring.fill(cap + extra);

    SdlogRingScan s = scan_of(ring);
    CHECK_MESSAGE(s.newest_seq == cap + extra - 1, "wrong head at rotation ", extra);
    CHECK_MESSAGE(s.next_index == (cap + extra) % cap, "wrong next index at rotation ", extra);
  }
}

TEST_CASE("survives many laps")
{
  FakeRing ring(64);
  ring.fill(64 * 40 + 7);

  SdlogRingScan s = scan_of(ring);
  CHECK(s.newest_seq == 64 * 40 + 6);
  CHECK(s.next_seq == 64 * 40 + 7);
}

TEST_CASE("the search is logarithmic, not linear")
{
  // The reason for the whole invariant: a full scan of a real ring would be tens
  // of seconds of SD reads at boot.
  FakeRing ring(4096);
  ring.fill(4096 + 1234);

  SdlogRingScan s = scan_of(ring);
  CHECK(s.newest_seq == 4096 + 1233);
  CHECK_MESSAGE(s.probes < 64, "used ", s.probes, " probes on a 4096-slot ring");
}

TEST_CASE("steps over a torn record and still finds the head")
{
  FakeRing ring(256);
  ring.fill(300);
  const uint32_t head_seq = 299;

  // Damage a record that is not the head itself.
  ring.tear(10);

  SdlogRingScan s = scan_of(ring);
  CHECK(s.newest_seq == head_seq);
}

TEST_CASE("steps over a whole torn sector")
{
  FakeRing ring(256);
  ring.fill(300);

  ring.tear_sector(64);   // 16 consecutive records, one sector's worth

  SdlogRingScan s = scan_of(ring);
  CHECK(s.newest_seq == 299);
  CHECK(s.corrupt_seen > 0);
}

TEST_CASE("a torn head falls back to the newest intact record")
{
  // Losing power mid-append is the expected case: the newest record is the one
  // that did not make it. Recovery must resume after the last good one, not
  // trust the damaged slot and not restart the ring.
  FakeRing ring(256);
  ring.fill(300);
  ring.tear(299 % 256);

  SdlogRingScan s = scan_of(ring);
  REQUIRE(s.any_records);
  CHECK(s.newest_seq == 298);
  CHECK(s.next_seq == 299);
}

TEST_CASE("rejects a record sitting in the wrong slot")
{
  // Intact bytes that do not belong here: a card from another unit, or a file
  // whose capacity changed under it. The CRC cannot catch this; the
  // seq % capacity invariant can.
  FakeRing ring(256);
  ring.fill(100);

  // Move a valid record to a slot it does not belong in.
  uint8_t stolen[SDLOG_RECORD_BYTES];
  memcpy(stolen, &ring.data[50 * SDLOG_RECORD_BYTES], SDLOG_RECORD_BYTES);
  memcpy(&ring.data[200 * SDLOG_RECORD_BYTES], stolen, SDLOG_RECORD_BYTES);

  SdlogRecord rec;
  REQUIRE(sdlog_decode(stolen, rec));
  CHECK(sdlog_ring_slot_matches(rec, 50, 256));
  CHECK_FALSE(sdlog_ring_slot_matches(rec, 200, 256));

  // The head is still 99; the misplaced record must not be mistaken for it.
  SdlogRingScan s = scan_of(ring);
  CHECK(s.newest_seq == 99);
}

TEST_CASE("reports an I/O failure rather than inventing a position")
{
  FakeRing ring(256);
  ring.fill(100);
  ring.fail_reads = true;

  SdlogRingScan out;
  CHECK_FALSE(sdlog_ring_scan(fake_read, &ring, ring.capacity, out));
}

TEST_CASE("a single-slot ring degenerates cleanly")
{
  FakeRing ring(1);
  SdlogRingScan empty = scan_of(ring);
  CHECK_FALSE(empty.any_records);

  ring.fill(5);
  SdlogRingScan s = scan_of(ring);
  CHECK(s.any_records);
  CHECK(s.newest_seq == 4);
  CHECK(s.next_index == 0);
}

TEST_CASE("zero capacity is refused, not divided by")
{
  FakeRing ring(1);
  SdlogRingScan out;
  CHECK_FALSE(sdlog_ring_scan(fake_read, &ring, 0, out));
}
