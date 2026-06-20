#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "backlight.h"

TEST_CASE("bl_pct_to_duty maps 0..100 to 0..255") {
  CHECK(bl_pct_to_duty(0) == 0);
  CHECK(bl_pct_to_duty(100) == 255);
  CHECK(bl_pct_to_duty(50) == 128);   // (50*255+50)/100 = 128 (rounded)
  CHECK(bl_pct_to_duty(200) == 255);  // over-range clamps
  for (uint8_t p = 1; p <= 100; ++p) {
    CHECK(bl_pct_to_duty(p) >= bl_pct_to_duty((uint8_t)(p - 1)));
  }
}

TEST_CASE("bl_should_standby: keep_awake never sleeps") {
  CHECK_FALSE(bl_should_standby(true, 600, 999999));
}

TEST_CASE("bl_should_standby: timeout 0 means never") {
  CHECK_FALSE(bl_should_standby(false, 0, 999999));
}

TEST_CASE("bl_should_standby: sleeps only once idle reaches the timeout") {
  CHECK_FALSE(bl_should_standby(false, 600, 599999));
  CHECK(bl_should_standby(false, 600, 600000));
  CHECK(bl_should_standby(false, 600, 600001));
}
