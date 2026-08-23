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
