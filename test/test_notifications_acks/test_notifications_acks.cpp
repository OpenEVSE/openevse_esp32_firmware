#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "notifications_acks.h"
#include <string.h>

TEST_CASE("round trip") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = notification_acks_decode("sg:1234;fg:3;", acks, NOTIFICATION_ACK_MAX);
  REQUIRE(n == 2);
  CHECK(0 == strcmp(acks[0].key, "sg"));
  CHECK(acks[0].token == 0x1234);
  CHECK(0 == strcmp(acks[1].key, "fg"));
  CHECK(acks[1].token == 3);

  char buf[128];
  CHECK(notification_acks_encode(acks, n, buf, sizeof(buf)) == 2);
  CHECK(0 == strcmp(buf, "sg:1234;fg:3;"));
}

TEST_CASE("empty and malformed blobs decode to nothing acked") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  CHECK(notification_acks_decode("", acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode(nullptr, acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode(";;;", acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode("nocolon;", acks, NOTIFICATION_ACK_MAX) == 0);
  CHECK(notification_acks_decode("sg:", acks, NOTIFICATION_ACK_MAX) == 0);
  // A truncated blob still yields the entries that did parse.
  CHECK(notification_acks_decode("sg:12;bro", acks, NOTIFICATION_ACK_MAX) == 1);
}

TEST_CASE("an over-long key is rejected rather than truncated into a collision") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  CHECK(notification_acks_decode("toolong:1;", acks, NOTIFICATION_ACK_MAX) == 0);
}

TEST_CASE("is_acked requires the token to still match") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = notification_acks_decode("fg:3;", acks, NOTIFICATION_ACK_MAX);
  CHECK(notification_acks_is_acked(acks, n, "fg", 3));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "fg", 4));   // a fourth trip
  CHECK_FALSE(notification_acks_is_acked(acks, n, "sg", 3));   // different rule
}

TEST_CASE("set adds, then replaces in place") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = 0;
  n = notification_acks_set(acks, n, NOTIFICATION_ACK_MAX, "sg", 1);
  CHECK(n == 1);
  n = notification_acks_set(acks, n, NOTIFICATION_ACK_MAX, "sg", 2);
  CHECK(n == 1);
  CHECK(notification_acks_is_acked(acks, n, "sg", 2));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "sg", 1));
}

TEST_CASE("a full store drops the new ack rather than evicting an old one") {
  NotificationAck acks[2];
  size_t n = 0;
  n = notification_acks_set(acks, n, 2, "aa", 1);
  n = notification_acks_set(acks, n, 2, "bb", 1);
  n = notification_acks_set(acks, n, 2, "cc", 1);
  CHECK(n == 2);
  CHECK(notification_acks_is_acked(acks, n, "aa", 1));
  CHECK(notification_acks_is_acked(acks, n, "bb", 1));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "cc", 1));
}

TEST_CASE("prune drops acks whose advisory is no longer live") {
  NotificationAck acks[NOTIFICATION_ACK_MAX];
  size_t n = notification_acks_decode("sg:1;fg:2;wl:1;", acks, NOTIFICATION_ACK_MAX);
  REQUIRE(n == 3);

  Notification live[2];
  live[0].id = "safety.ground_check"; live[0].key = "sg";
  live[0].category = NOTIFICATION_SAFETY; live[0].severity = NOTIFICATION_CRITICAL;
  live[0].sticky = true; live[0].token = 1;
  live[1].id = "wear.relay_life"; live[1].key = "wl";
  live[1].category = NOTIFICATION_WEAR; live[1].severity = NOTIFICATION_INFO;
  live[1].sticky = false; live[1].token = 1;

  n = notification_acks_prune(acks, n, live, 2);
  CHECK(n == 2);
  CHECK(notification_acks_is_acked(acks, n, "sg", 1));
  CHECK(notification_acks_is_acked(acks, n, "wl", 1));
  CHECK_FALSE(notification_acks_is_acked(acks, n, "fg", 2));
}

TEST_CASE("encode stops cleanly when the buffer runs out") {
  NotificationAck acks[3] = { { "aa", 1 }, { "bb", 2 }, { "cc", 3 } };
  char buf[8];
  size_t written = notification_acks_encode(acks, 3, buf, sizeof(buf));
  CHECK(written < 3);
  CHECK(strlen(buf) < sizeof(buf));
  // Whatever fits must still be re-readable.
  NotificationAck back[NOTIFICATION_ACK_MAX];
  CHECK(notification_acks_decode(buf, back, NOTIFICATION_ACK_MAX) == written);
}
