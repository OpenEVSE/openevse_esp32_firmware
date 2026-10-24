#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "mqtt_reconnect.h"

static const uint32_t INTERVAL = 5000;
static const uint32_t DAY_MS = 86400000UL;

TEST_CASE("mqtt_reconnect_due: force retries at once") {
  CHECK(mqtt_reconnect_due(true, 1000, 1000, INTERVAL));
}

TEST_CASE("mqtt_reconnect_due: waits for the interval") {
  CHECK_FALSE(mqtt_reconnect_due(false, 1000, 1000, INTERVAL));
  CHECK_FALSE(mqtt_reconnect_due(false, 1000, 5999, INTERVAL));
  CHECK(mqtt_reconnect_due(false, 1000, 6000, INTERVAL));
}

// Issue #1367: after ~24.86 days millis() read as a signed long goes negative,
// and the old "now > 0" check never retried after a connection dropped.
TEST_CASE("mqtt_reconnect_due: retries past 2^31 ms of uptime") {
  uint32_t days[] = { 24, 25, 39, 40, 49 };
  for (uint32_t d : days) {
    uint32_t now = d * DAY_MS;
    // Connected for a long time, then dropped: last attempt long ago.
    CHECK(mqtt_reconnect_due(false, 1000, now, INTERVAL));
    // Retry right after a disconnect.
    CHECK(mqtt_reconnect_due(true, 1000, now, INTERVAL));
    // Repeated failures keep retrying every interval.
    CHECK_FALSE(mqtt_reconnect_due(false, now, now + INTERVAL - 1, INTERVAL));
    CHECK(mqtt_reconnect_due(false, now, now + INTERVAL, INTERVAL));
  }
}

TEST_CASE("mqtt_reconnect_due: millis() wrap at ~49.7 days") {
  uint32_t last = 0xFFFFF000;  // 4096 ms before the wrap
  CHECK_FALSE(mqtt_reconnect_due(false, last, 500, INTERVAL));  // 4596 ms elapsed
  CHECK(mqtt_reconnect_due(false, last, 904, INTERVAL));        // 5000 ms elapsed
}
