// src/fault_text.h -- what a fault means, and what to do about it.
//
// The panel names every fault distinctly (state_word() in lvgl_tft), but a name
// alone tells whoever is standing at the charger nothing about what to do next,
// and these want different responses: a GFCI trip is a reset, no ground is an
// electrician, over-temp is airflow. This table carries that second half.
//
// Deliberately free of LVGL, Arduino and openevse.h so the copy can be unit
// tested on the host as data. The state codes below mirror OPENEVSE_STATE_*;
// fault_screen.cpp static_asserts the two sets against each other, so the
// device build fails loudly if the library ever renumbers them.
#ifndef __FAULT_TEXT_H
#define __FAULT_TEXT_H

#include <stdint.h>

#define FAULT_STATE_VENT_REQUIRED         4
#define FAULT_STATE_DIODE_CHECK_FAILED    5
#define FAULT_STATE_GFI_FAULT             6
#define FAULT_STATE_NO_EARTH_GROUND       7
#define FAULT_STATE_STUCK_RELAY           8
#define FAULT_STATE_GFI_SELF_TEST_FAILED  9
#define FAULT_STATE_OVER_TEMPERATURE     10
#define FAULT_STATE_OVER_CURRENT         11
#define FAULT_STATE_RELAY_CLOSURE_FAULT  14
#define FAULT_STATE_PP_SHORTED           15
#define FAULT_STATE_PP_MISSING           16
#define FAULT_STATE_EEPROM_FAILURE       17

// Layout budget, in characters, for the 480x320 panel. The unit test enforces
// these so over-long copy fails on the host rather than silently clipping or
// wrapping on the glass. The title is set at montserrat_32 beside a 64 px mark,
// the body at montserrat_16 across the full width.
#define FAULT_TITLE_MAX  16
#define FAULT_WHAT_MAX  140   // wraps to at most three lines at 44 ch
#define FAULT_STEP_MAX   46   // one line each, never wrapped
#define FAULT_STEPS_MAX   3

struct FaultText {
  const char *title;                  // the fault's name, title case (see the
                                      // house style note in fault_text.cpp)
  const char *what;                   // one or two sentences: what happened
  const char *steps[FAULT_STEPS_MAX]; // what to do, most likely to help first;
                                      // unused slots are NULL
};

// The entry for a fault state, or NULL if the state is not a fault. Points at
// static storage; never freed.
const FaultText *fault_text(uint8_t evse_state);

// True when the state is one the fault screen should take over for. Exactly the
// states fault_text() answers for.
bool state_is_fault(uint8_t evse_state);

#endif // __FAULT_TEXT_H
