// Fault copy. Pure data; see fault_text.h for why it is kept free of LVGL and
// openevse.h.
//
// House style for the copy, so later additions match:
//  - Titles are title case, not the ring's shouting capitals: this is a heading
//    read at arm's length at 28 px, where all-caps costs legibility and gains
//    nothing. Acronyms stay up (PP, GFCI, EEPROM). state_word() keeps its caps
//    for the compact status word inside the ring.
//  - "what" states what happened and, where it matters, whether charging can
//    resume on its own. No jargon the person at the charger cannot act on.
//  - "steps" are ordered most-likely-to-help first, and are things a non-
//    electrician can safely do -- except where doing nothing is the safe answer.
//  - No step tells anyone to press a front button: not every unit has one.
//    Everything reduces to unplug/replug or a power cycle.
//  - Three faults deliberately say to stop using the charger: NO GROUND (the
//    fault path is defeated), STUCK RELAY (the connector may be live), and
//    GFCI SELF TEST (the ground-fault protection is unverified). The rest stay
//    in try-this territory.
#include "fault_text.h"

#include <stddef.h>

static const FaultText FAULTS[] = {
  { "Vent Required",
    "The vehicle asked for a ventilated area. This charger cannot provide one, so charging stopped.",
    { "Unplug the vehicle, then plug it back in",
      "Check the cable and connector for damage",
      "If it keeps happening, have it checked" } },

  { "Diode Check",
    "The vehicle's pilot diode was not detected. Charging is blocked as a safety check.",
    { "Unplug the vehicle, then plug it back in",
      "Inspect the connector for damage or moisture",
      "Try another cable or another vehicle" } },

  { "GFCI Trip",
    "A ground fault was detected and charging stopped. The charger will retry on its own.",
    { "Unplug the vehicle",
      "Check the inlet and connector for water",
      "If it repeats, stop and call an electrician" } },

  { "No Ground",
    "No earth ground was found on the supply. The charger will not deliver power without one.",
    { "Do not use the charger until this is fixed",
      "Have an electrician check the circuit ground",
      "This is wiring, not the vehicle or the cable" } },

  { "Stuck Relay",
    "The contactor did not open when told to. Power may still be present at the connector.",
    { "Switch the circuit off at the breaker",
      "Do not plug in a vehicle",
      "The charger needs service before further use" } },

  { "GFCI Self-Test",
    "The ground-fault self-test failed at start-up, so that protection cannot be trusted.",
    { "Switch the charger off, wait, switch it on",
      "Do not use it if the test fails again",
      "A repeat failure means it needs service" } },

  { "Over Temp",
    "The charger is too hot and stopped to protect itself. It resumes once it cools.",
    { "Clear anything blocking the vents",
      "Shade it from direct sun if you can",
      "If it repeats when cool, have it checked" } },

  { "Over Current",
    "The vehicle drew more current than was offered, so charging stopped.",
    { "Unplug the vehicle, then plug it back in",
      "Lower the max current in Charger settings",
      "If it repeats, have the vehicle checked" } },

  { "Relay Fault",
    "The contactor did not respond as commanded when charging was starting.",
    { "Switch the charger off, wait, switch it on",
      "If it repeats, the contactor needs service",
      "Do not leave it charging unattended" } },

  { "EEPROM Failure",
    "The charger could not read or write its stored settings.",
    { "Switch the charger off, wait, switch it on",
      "Check the settings are still correct",
      "If it repeats, the controller needs service" } },

  { "PP Missing",
    "The cable's proximity pilot was not detected, so its current rating is unknown.",
    { "Reseat the cable at the charger socket",
      "Inspect both ends for bent or dirty pins",
      "Try a different cable" } },

  { "PP Shorted",
    "The cable's proximity pilot line reads as shorted.",
    { "Unplug the cable at both ends",
      "Look for damage or moisture in the socket",
      "Try a different cable" } },
};

// Parallel to FAULTS, rather than a sparse 256-entry table: the fault codes are
// not contiguous (12 and 13 are unused, PP/EEPROM sit at 14-17) and a switch
// keeps the two lists visibly in step.
const FaultText *fault_text(uint8_t evse_state)
{
  switch(evse_state) {
    case FAULT_STATE_VENT_REQUIRED:        return &FAULTS[0];
    case FAULT_STATE_DIODE_CHECK_FAILED:   return &FAULTS[1];
    case FAULT_STATE_GFI_FAULT:            return &FAULTS[2];
    case FAULT_STATE_NO_EARTH_GROUND:      return &FAULTS[3];
    case FAULT_STATE_STUCK_RELAY:          return &FAULTS[4];
    case FAULT_STATE_GFI_SELF_TEST_FAILED: return &FAULTS[5];
    case FAULT_STATE_OVER_TEMPERATURE:     return &FAULTS[6];
    case FAULT_STATE_OVER_CURRENT:         return &FAULTS[7];
    case FAULT_STATE_RELAY_CLOSURE_FAULT:  return &FAULTS[8];
    case FAULT_STATE_EEPROM_FAILURE:       return &FAULTS[9];
    case FAULT_STATE_PP_MISSING:           return &FAULTS[10];
    case FAULT_STATE_PP_SHORTED:           return &FAULTS[11];
    default:                               return NULL;
  }
}

bool state_is_fault(uint8_t evse_state)
{
  return NULL != fault_text(evse_state);
}
