// Host-side tests for the fault copy (fault_text.cpp).
//
// The copy is the substance of the fault screen, so it is tested as data: every
// fault answers, no non-fault does, and nothing exceeds the width budget the
// 480x320 layout was drawn to. Over-long copy fails here rather than clipping
// silently on the glass, where nobody is watching until something is wrong.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "fault_text.h"

#include <string.h>

static const uint8_t FAULTS[] = {
  FAULT_STATE_VENT_REQUIRED,        FAULT_STATE_DIODE_CHECK_FAILED,
  FAULT_STATE_GFI_FAULT,            FAULT_STATE_NO_EARTH_GROUND,
  FAULT_STATE_STUCK_RELAY,          FAULT_STATE_GFI_SELF_TEST_FAILED,
  FAULT_STATE_OVER_TEMPERATURE,     FAULT_STATE_OVER_CURRENT,
  FAULT_STATE_RELAY_CLOSURE_FAULT,  FAULT_STATE_PP_SHORTED,
  FAULT_STATE_PP_MISSING,           FAULT_STATE_EEPROM_FAILURE,
};
static const size_t N_FAULTS = sizeof(FAULTS) / sizeof(FAULTS[0]);

TEST_CASE("every fault state has copy") {
  for(size_t i = 0; i < N_FAULTS; i++) {
    const FaultText *f = fault_text(FAULTS[i]);
    REQUIRE(f);
    CHECK(strlen(f->title) > 0);
    CHECK(strlen(f->what) > 0);
    // A page that names a fault and then offers nothing to do is the state this
    // screen exists to replace.
    CHECK(f->steps[0] != NULL);
  }
}

TEST_CASE("non-fault states have no copy") {
  // Starting, not connected, connected, charging, sleeping, disabled, and the
  // two codes the controller does not use.
  const uint8_t ok[] = {0, 1, 2, 3, 12, 13, 18, 100, 254, 255};
  for(size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
    CHECK_FALSE(fault_text(ok[i]));
    CHECK_FALSE(state_is_fault(ok[i]));
  }
}

TEST_CASE("state_is_fault agrees with fault_text") {
  for(int s = 0; s <= 255; s++) {
    CHECK(state_is_fault((uint8_t)s) == (bool)fault_text((uint8_t)s));
  }
}

TEST_CASE("copy fits the layout budget") {
  for(size_t i = 0; i < N_FAULTS; i++) {
    const FaultText *f = fault_text(FAULTS[i]);
    REQUIRE(f);
    CHECK(strlen(f->title) <= FAULT_TITLE_MAX);
    CHECK(strlen(f->what) <= FAULT_WHAT_MAX);
    for(int s = 0; s < FAULT_STEPS_MAX && f->steps[s]; s++) {
      CHECK(strlen(f->steps[s]) <= FAULT_STEP_MAX);
    }
  }
}

TEST_CASE("steps are packed, not sparse") {
  // The renderer stops at the first NULL, so a gap would silently drop the
  // steps after it.
  for(size_t i = 0; i < N_FAULTS; i++) {
    const FaultText *f = fault_text(FAULTS[i]);
    REQUIRE(f);
    bool seen_null = false;
    for(int s = 0; s < FAULT_STEPS_MAX; s++) {
      if(!f->steps[s]) { seen_null = true; continue; }
      CHECK_FALSE(seen_null);
    }
  }
}

TEST_CASE("no two faults share a title") {
  // Distinct names are the whole point; a copy/paste that duplicates one would
  // send someone to the wrong remedy.
  for(size_t i = 0; i < N_FAULTS; i++) {
    for(size_t j = i + 1; j < N_FAULTS; j++) {
      CHECK(strcmp(fault_text(FAULTS[i])->title, fault_text(FAULTS[j])->title) != 0);
    }
  }
}
