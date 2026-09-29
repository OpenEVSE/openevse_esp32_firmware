#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "notifications_rules.h"
#include <string.h>

// A charger with nothing to complain about: every safety check on, no faults,
// no thermal event, relay health known and healthy.
static NotificationInputs clean()
{
  NotificationInputs in = {};
  in.ground_check = in.gfci_check = in.relay_check = true;
  in.diode_check = in.vent_check = in.temp_check = true;
  in.temp_valid = true;
  in.temp_c_x10 = 250;
  in.panic_temp_c = 65;
  in.relay_health_known = true;
  in.relay_life_pct = 100;
  in.settings_flags = 0x0001;
  return in;
}

static bool has(const Notification *l, size_t n, const char *id)
{
  for(size_t i = 0; i < n; i++) {
    if(0 == strcmp(l[i].id, id)) return true;
  }
  return false;
}

static const Notification *find(const Notification *l, size_t n, const char *id)
{
  for(size_t i = 0; i < n; i++) {
    if(0 == strcmp(l[i].id, id)) return &l[i];
  }
  return nullptr;
}

TEST_CASE("a clean charger raises nothing") {
  Notification out[NOTIFICATION_MAX];
  CHECK(notifications_evaluate(clean(), out, NOTIFICATION_MAX) == 0);
  CHECK(notifications_worst(out, 0) == -1);
}

TEST_CASE("each safety check raises its own advisory when disabled") {
  Notification out[NOTIFICATION_MAX];

  NotificationInputs in = clean(); in.ground_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.ground_check"));

  in = clean(); in.gfci_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.gfci_check"));

  in = clean(); in.relay_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.relay_check"));

  in = clean(); in.diode_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.diode_check"));

  in = clean(); in.vent_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.vent_check"));

  in = clean(); in.temp_check = false;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "safety.temp_check"));
}

TEST_CASE("ground and gfci are critical, the other four are warnings") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.ground_check = in.gfci_check = in.relay_check = false;
  in.diode_check = in.vent_check = in.temp_check = false;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(n == 6);
  CHECK(find(out, n, "safety.ground_check")->severity == NOTIFICATION_CRITICAL);
  CHECK(find(out, n, "safety.gfci_check")->severity == NOTIFICATION_CRITICAL);
  CHECK(find(out, n, "safety.relay_check")->severity == NOTIFICATION_WARNING);
  CHECK(find(out, n, "safety.diode_check")->severity == NOTIFICATION_WARNING);
  CHECK(find(out, n, "safety.vent_check")->severity == NOTIFICATION_WARNING);
  CHECK(find(out, n, "safety.temp_check")->severity == NOTIFICATION_WARNING);
  CHECK(notifications_max_severity(out, n) == NOTIFICATION_CRITICAL);
}

TEST_CASE("every safety advisory is sticky and carries the settings flags in its token") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.ground_check = false;
  in.settings_flags = 0x1234;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *g = find(out, n, "safety.ground_check");
  CHECK(g->sticky);
  CHECK(g->token == 0x1234);

  // Changing an unrelated setting moves the token, which is what voids an ack.
  in.settings_flags = 0x1235;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "safety.ground_check")->token == 0x1235);
}

TEST_CASE("fault counters raise, and their token is the count") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.gfci_count = 3;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *f = find(out, n, "fault.gfci_tripped");
  REQUIRE(f != nullptr);
  CHECK(f->severity == NOTIFICATION_CRITICAL);
  CHECK_FALSE(f->sticky);
  CHECK(f->token == 3);

  in = clean(); in.no_ground_count = 1;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "fault.no_ground")->token == 1);

  in = clean(); in.stuck_relay_count = 7;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "fault.stuck_relay")->token == 7);
}

TEST_CASE("zero fault counters raise nothing") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK_FALSE(has(out, n, "fault.gfci_tripped"));
  CHECK_FALSE(has(out, n, "fault.no_ground"));
  CHECK_FALSE(has(out, n, "fault.stuck_relay"));
}

TEST_CASE("thermal throttling raises while it is active") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.temp_throttling = true;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *t = find(out, n, "thermal.throttling");
  REQUIRE(t != nullptr);
  CHECK(t->severity == NOTIFICATION_WARNING);
  CHECK(t->sticky);
}

TEST_CASE("high temp raises within the margin of the panic threshold") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.panic_temp_c = 65;          // margin 5 => raises at 60.0 C

  in.temp_c_x10 = 599;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));

  in.temp_c_x10 = 600;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));

  in.temp_c_x10 = 700;
  CHECK(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));
}

TEST_CASE("high temp is skipped when the reading or the threshold is unknown") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.temp_c_x10 = 900;

  in.temp_valid = false; in.panic_temp_c = 65;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));

  in.temp_valid = true; in.panic_temp_c = 0;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "thermal.high_temp"));
}

TEST_CASE("relay life steps from info to warning at the two thresholds") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();

  in.relay_life_pct = 21;
  CHECK_FALSE(has(out, notifications_evaluate(in, out, NOTIFICATION_MAX), "wear.relay_life"));

  in.relay_life_pct = 20;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_INFO);

  in.relay_life_pct = 6;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_INFO);

  in.relay_life_pct = 5;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_WARNING);

  in.relay_life_pct = 0;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->severity == NOTIFICATION_WARNING);
}

TEST_CASE("the relay life token steps with severity, not with every percent") {
  // Otherwise an ack would be voided by ordinary wear ticking 20 -> 19.
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();

  in.relay_life_pct = 20;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  uint32_t at20 = find(out, n, "wear.relay_life")->token;

  in.relay_life_pct = 9;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->token == at20);

  in.relay_life_pct = 5;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "wear.relay_life")->token != at20);
}

TEST_CASE("cold opens, drift, recoveries and relay thermal") {
  Notification out[NOTIFICATION_MAX];

  NotificationInputs in = clean(); in.relay_cold_open_count = 2;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *c = find(out, n, "wear.relay_cold_open");
  REQUIRE(c != nullptr);
  CHECK(c->severity == NOTIFICATION_WARNING);
  CHECK(c->token == 2);

  in = clean(); in.relay_transit_drift = true;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *d = find(out, n, "wear.relay_transit_drift");
  REQUIRE(d != nullptr);
  CHECK(d->severity == NOTIFICATION_WARNING);
  CHECK(d->sticky);

  in = clean(); in.relay_stuck_recovery_count = 4;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  const Notification *r = find(out, n, "wear.stuck_relay_recovery");
  REQUIRE(r != nullptr);
  CHECK(r->severity == NOTIFICATION_INFO);
  CHECK(r->token == 4);

  in = clean(); in.relay_thermal_warning_level = 1;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "thermal.relay_thermal")->severity == NOTIFICATION_INFO);

  in.relay_thermal_warning_level = 2;
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(find(out, n, "thermal.relay_thermal")->severity == NOTIFICATION_WARNING);
}

TEST_CASE("no relay rule exists at all when relay health is unknown") {
  // A controller without the RELAY_HEALTH feature must look like a controller
  // with nothing to say, not like one reporting a confident zero.
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.relay_health_known = false;
  in.relay_life_pct = 0;
  in.relay_cold_open_count = 9;
  in.relay_transit_drift = true;
  in.relay_stuck_recovery_count = 9;
  in.relay_thermal_warning_level = 2;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(n == 0);
}

TEST_CASE("worst picks highest severity, earliest in table order among equals") {
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.vent_check = false;         // warning, earlier in the table
  in.gfci_count = 1;             // critical, later
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(0 == strcmp(out[notifications_worst(out, n)].id, "fault.gfci_tripped"));

  in = clean();
  in.relay_check = false;        // warning
  in.diode_check = false;        // warning, later in the table
  n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(0 == strcmp(out[notifications_worst(out, n)].id, "safety.relay_check"));
}

TEST_CASE("evaluate never writes past max") {
  Notification out[3];
  NotificationInputs in = clean();
  in.ground_check = in.gfci_check = in.relay_check = false;
  in.diode_check = in.vent_check = in.temp_check = false;
  CHECK(notifications_evaluate(in, out, 3) == 3);
}

TEST_CASE("every id and storage key is unique") {
  // The ack blob is keyed on `key`; a duplicate would silently mute two things.
  Notification out[NOTIFICATION_MAX];
  NotificationInputs in = clean();
  in.ground_check = in.gfci_check = in.relay_check = false;
  in.diode_check = in.vent_check = in.temp_check = false;
  in.gfci_count = in.no_ground_count = in.stuck_relay_count = 1;
  in.temp_throttling = true;
  in.temp_c_x10 = 900;
  in.relay_life_pct = 1;
  in.relay_cold_open_count = 1;
  in.relay_transit_drift = true;
  in.relay_stuck_recovery_count = 1;
  in.relay_thermal_warning_level = 2;
  size_t n = notifications_evaluate(in, out, NOTIFICATION_MAX);
  CHECK(n == 16);
  for(size_t i = 0; i < n; i++) {
    for(size_t j = i + 1; j < n; j++) {
      CHECK(0 != strcmp(out[i].id, out[j].id));
      CHECK(0 != strcmp(out[i].key, out[j].key));
    }
  }
}
