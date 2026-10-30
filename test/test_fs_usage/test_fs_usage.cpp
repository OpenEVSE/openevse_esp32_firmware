#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "fs_usage.h"

static int total_reads;
static int used_reads;
static uint32_t used_value;

static uint32_t read_total() { total_reads++; return 3375104; }
static uint32_t read_used() { used_reads++; return used_value; }

static void reset(uint32_t used = 57344)
{
  total_reads = 0;
  used_reads = 0;
  used_value = used;
}

TEST_CASE("first get samples inline") {
  reset();
  FsUsageCache cache(read_total, read_used, 30000);
  uint32_t total = 0, used = 0;
  cache.get(1000, total, used);
  CHECK(total == 3375104);
  CHECK(used == 57344);
  CHECK(used_reads == 1);
}

TEST_CASE("without a background sampler, get refreshes once the sample is stale") {
  reset();
  FsUsageCache cache(read_total, read_used, 30000);
  uint32_t total, used;
  cache.get(1000, total, used);
  used_value = 61440;
  cache.get(30999, total, used);
  CHECK(used == 57344);
  CHECK(used_reads == 1);
  cache.get(31000, total, used);
  CHECK(used == 61440);
  CHECK(used_reads == 2);
}

TEST_CASE("with a background sampler, get never touches the filesystem after the first sample") {
  reset();
  FsUsageCache cache(read_total, read_used, 30000);
  cache.setBackgroundSampler();
  cache.refresh(0);
  uint32_t total, used;
  cache.get(1000000, total, used);
  CHECK(used == 57344);
  CHECK(used_reads == 1);
}

TEST_CASE("refresh updates used and reads the total only once") {
  reset();
  FsUsageCache cache(read_total, read_used, 30000);
  cache.setBackgroundSampler();
  cache.refresh(0);
  used_value = 65536;
  cache.refresh(60000);
  uint32_t total, used;
  cache.get(60001, total, used);
  CHECK(total == 3375104);
  CHECK(used == 65536);
  CHECK(total_reads == 1);
  CHECK(used_reads == 2);
}

TEST_CASE("staleness check survives the millis() wrap") {
  reset();
  FsUsageCache cache(read_total, read_used, 30000);
  uint32_t total, used;
  cache.get(0xFFFFF000, total, used);
  cache.get(0x00000100, total, used);   // 4352 ms later
  CHECK(used_reads == 1);
  cache.get(0x00007000, total, used);   // 32768 ms later
  CHECK(used_reads == 2);
}

TEST_CASE("a sample taken at millis() == 0 still counts as sampled") {
  reset();
  FsUsageCache cache(read_total, read_used, 30000);
  cache.setBackgroundSampler();
  cache.refresh(0);
  uint32_t total, used;
  cache.get(0, total, used);
  CHECK(used_reads == 1);
}
