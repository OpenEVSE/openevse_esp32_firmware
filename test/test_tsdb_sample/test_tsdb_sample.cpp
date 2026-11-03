#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "tsdb_sample.h"

TEST_CASE("scale/unscale round-trips per column") {
  EnergySample s; s.amps=32.0; s.volts=240; s.power_w=7680; s.energy_wh_delta=128;
  s.temp_c=25.0; s.soc=80; s.pilot_a=32;
  int16_t v[TSDB_NUM_COLS];
  tsdb_scale_sample(s, v);
  CHECK(v[TSDB_COL_AMPS]  == 320);     // 32.0 * 10
  CHECK(v[TSDB_COL_VOLTS] == 240);
  CHECK(v[TSDB_COL_POWER] == 768);     // 7680 / 10
  CHECK(v[TSDB_COL_ENERGY]== 128);
  CHECK(v[TSDB_COL_TEMP]  == 250);     // 25.0 * 10
  CHECK(v[TSDB_COL_SOC]   == 80);
  CHECK(v[TSDB_COL_PILOT] == 32);
  CHECK(tsdb_unscale(TSDB_COL_AMPS,  320) == doctest::Approx(32.0));
  CHECK(tsdb_unscale(TSDB_COL_POWER, 768) == doctest::Approx(7680.0));
  CHECK(tsdb_unscale(TSDB_COL_TEMP,  250) == doctest::Approx(25.0));
}

TEST_CASE("soc -1 sentinel preserved") {
  EnergySample s; s.soc=-1;
  int16_t v[TSDB_NUM_COLS]; tsdb_scale_sample(s, v);
  CHECK(v[TSDB_COL_SOC] == -1);
  CHECK(tsdb_unscale(TSDB_COL_SOC, -1) == -1);
}

TEST_CASE("overflow clamps to int16 range") {
  EnergySample s; s.power_w = 9'000'000;   // absurd, /10 still > 32767
  int16_t v[TSDB_NUM_COLS]; tsdb_scale_sample(s, v);
  CHECK(v[TSDB_COL_POWER] == 32767);
}

TEST_CASE("param names match column count and order") {
  CHECK(std::string(TSDB_PARAM_NAMES[TSDB_COL_AMPS])  == "amps");
  CHECK(std::string(TSDB_PARAM_NAMES[TSDB_COL_PILOT]) == "pilot");
}

TEST_CASE("sample cadence: card gets 6x the resolution of flash") {
  CHECK(tsdb_sample_interval_ms(true,  false) == 60000UL);
  CHECK(tsdb_sample_interval_ms(false, false) == 5UL * 60000UL);
  CHECK(tsdb_sample_interval_ms(true,  true)  == 10000UL);
  CHECK(tsdb_sample_interval_ms(false, true)  == 60000UL);
  // Idle is never faster than charging on either store.
  CHECK(tsdb_sample_interval_ms(false, true)  >= tsdb_sample_interval_ms(true, true));
  CHECK(tsdb_sample_interval_ms(false, false) >= tsdb_sample_interval_ms(true, false));
}

// A day's samples can sit in two stores: flash until the card came up, the card
// after. Each sample is written to exactly one, so the day is their union.
TEST_CASE("day rollup merges the two stores") {
  EnergyDayAgg flash; flash.count = 300; flash.energy_wh = 4000; flash.temp_max = 310; flash.temp_min = 120;
  EnergyDayAgg card;  card.count  = 900; card.energy_wh  = 6500; card.temp_max  = 280; card.temp_min  = 90;

  EnergyDayAgg day = energy_day_merge(flash, card);
  CHECK(day.count == 1200);
  CHECK(day.energy_wh == 10500);
  CHECK(day.temp_max == 310);
  CHECK(day.temp_min == 90);
}

TEST_CASE("day rollup ignores an empty store's extremes") {
  // An empty part's zero max/min must not leak in: a cold day at -5.0 C stays
  // at -5.0 C, not 0.
  EnergyDayAgg empty;
  EnergyDayAgg card; card.count = 10; card.energy_wh = 50; card.temp_max = -20; card.temp_min = -50;

  EnergyDayAgg a = energy_day_merge(empty, card);
  EnergyDayAgg b = energy_day_merge(card, empty);
  for (const EnergyDayAgg &d : {a, b}) {
    CHECK(d.count == 10);
    CHECK(d.energy_wh == 50);
    CHECK(d.temp_max == -20);
    CHECK(d.temp_min == -50);
  }
  CHECK(energy_day_merge(empty, empty).count == 0);
}
