// Host-side tests for the event-log free-space guard (event_log_space.h).
//
// The guard trades a real LittleFS measurement (30-140 ms) for a cached
// estimate. These tests pin when it is allowed to trust that estimate, so the
// stall cannot quietly return and the 8 KB reserve cannot quietly erode.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "event_log_space.h"

static size_t fsFree = 0;
static int measurements = 0;

static size_t measure()
{
  measurements++;
  return fsFree;
}

static void reset(size_t free)
{
  fsFree = free;
  measurements = 0;
}

static const size_t PLENTY = EVENTLOG_MIN_FREE_BYTES + EVENTLOG_FREE_HEADROOM_BYTES + 100000;
static const size_t NEAR = EVENTLOG_MIN_FREE_BYTES + 1000;

TEST_CASE("the first check measures")
{
  reset(PLENTY);
  EventLogSpaceGuard guard(measure);
  CHECK(guard.canWrite(1000));
  CHECK(1 == measurements);
}

TEST_CASE("with plenty of room the estimate is trusted until the sample interval")
{
  reset(PLENTY);
  EventLogSpaceGuard guard(measure);
  guard.canWrite(1000);
  for(uint32_t t = 1000; t < 1000 + EVENTLOG_FREE_SAMPLE_MS; t += 1000) {
    CHECK(guard.canWrite(t));
  }
  CHECK(1 == measurements);

  guard.canWrite(1000 + EVENTLOG_FREE_SAMPLE_MS);
  CHECK(2 == measurements);
}

TEST_CASE("bytes written are deducted from the estimate")
{
  reset(PLENTY);
  EventLogSpaceGuard guard(measure);
  guard.canWrite(1000);
  guard.noteWritten(1234);
  CHECK((int32_t)PLENTY - 1234 == guard.estimate());
}

TEST_CASE("an oversized entry is deducted in full, not capped")
{
  reset(EVENTLOG_MIN_FREE_BYTES + 600);
  EventLogSpaceGuard guard(measure);
  CHECK(guard.canWrite(1000));
  guard.noteWritten(2000);
  CHECK(false == guard.canWrite(1001));
}

TEST_CASE("near the reserve a write made elsewhere is caught by the re-measure")
{
  reset(EVENTLOG_MIN_FREE_BYTES + EVENTLOG_FREE_HEADROOM_BYTES - 1);
  EventLogSpaceGuard guard(measure);
  CHECK(guard.canWrite(1000));

  // Another writer takes the filesystem below the reserve, well inside the
  // 30 s sample interval.
  fsFree = EVENTLOG_MIN_FREE_BYTES - 1;
  CHECK(false == guard.canWrite(1000 + EVENTLOG_NEAR_SAMPLE_MS));
  CHECK(2 == measurements);
}

TEST_CASE("near the reserve re-measurement is rate limited")
{
  reset(NEAR);
  EventLogSpaceGuard guard(measure);
  guard.canWrite(1000);
  for(uint32_t t = 1000; t < 1000 + EVENTLOG_NEAR_SAMPLE_MS; t += 100) {
    guard.canWrite(t);
  }
  CHECK(1 == measurements);
}

TEST_CASE("a full filesystem does not measure on every entry")
{
  reset(EVENTLOG_MIN_FREE_BYTES - 1);
  EventLogSpaceGuard guard(measure);
  for(uint32_t t = 1000; t < 1000 + EVENTLOG_NEAR_SAMPLE_MS; t += 10) {
    CHECK(false == guard.canWrite(t));
  }
  CHECK(1 == measurements);
}

TEST_CASE("space freed while full is noticed at the next re-measure")
{
  reset(EVENTLOG_MIN_FREE_BYTES - 1);
  EventLogSpaceGuard guard(measure);
  CHECK(false == guard.canWrite(1000));

  fsFree = PLENTY;
  CHECK(false == guard.canWrite(1001));
  CHECK(guard.canWrite(1000 + EVENTLOG_NEAR_SAMPLE_MS));
}

TEST_CASE("millis() wrap does not freeze the sample")
{
  reset(PLENTY);
  EventLogSpaceGuard guard(measure);
  guard.canWrite(UINT32_MAX - 1000);
  guard.canWrite((uint32_t)(UINT32_MAX - 1000 + EVENTLOG_FREE_SAMPLE_MS));
  CHECK(2 == measurements);
}
