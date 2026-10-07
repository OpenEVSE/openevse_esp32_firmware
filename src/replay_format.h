#ifndef REPLAY_FORMAT_H
#define REPLAY_FORMAT_H

// Record layouts for the replay recorder (replay_recorder.*), kept free of
// firmware dependencies so the encoding and claim diffing are unit tested on
// the host (test/test_replay_format).

#include <stddef.h>
#include <stdint.h>

#define REPLAY_FORMAT_VERSION 1

// One periodic sample: the inputs the simulator replays (solar, grid, site
// power, voltage, temperature, plug state) and the outcome it is compared
// against (EVSE state, pilot, current). Scaled integers keep the ring small.
#define REPLAY_FLAG_VEHICLE        (1 << 0)
#define REPLAY_FLAG_CHARGING       (1 << 1)
#define REPLAY_FLAG_TARGET_ACTIVE  (1 << 2)
#define REPLAY_FLAG_ECO            (1 << 3)
#define REPLAY_FLAG_SHAPER         (1 << 4)
#define REPLAY_FLAG_RFID_AUTH      (1 << 5)

#define REPLAY_TEMP_INVALID   INT16_MIN
#define REPLAY_SOC_INVALID    (-1)

struct ReplaySample
{
  uint32_t up;            // seconds since boot
  int16_t  solar_dw;      // W / 10
  int16_t  grid_ie_dw;    // W / 10 (import positive)
  int16_t  live_pwr_dw;   // W / 10
  uint16_t volts_dv;      // V * 10
  uint16_t amps_ca;       // A * 100
  int16_t  temp_dc;       // degC * 10, REPLAY_TEMP_INVALID when unknown
  uint16_t session_dwh;   // Wh / 10
  uint8_t  pilot;         // A
  uint8_t  evse_state;    // OPENEVSE_STATE_*
  int8_t   soc;           // %, REPLAY_SOC_INVALID when unknown
  uint8_t  target_current;// A, what the claims ask for
  uint8_t  flags;         // REPLAY_FLAG_*
};

// Column names, in the order replay_sample_row() writes them.
extern const char *const REPLAY_SAMPLE_COLUMNS[];
extern const size_t REPLAY_SAMPLE_COLUMN_COUNT;

// Scale engineering units into a sample (clamping to the field ranges).
void replay_sample_set(ReplaySample &s, uint32_t up, double solar_w, double grid_ie_w,
                       double live_pwr_w, double volts, double amps, bool temp_valid,
                       double temp_c, double session_wh, long pilot, uint8_t evse_state,
                       int soc, uint32_t target_current, uint8_t flags);

// Write one sample as a JSON array, e.g. [120,350,-120,...]. Returns the
// number of characters written (excluding the terminator), or 0 if `len` was
// too small.
size_t replay_sample_row(const ReplaySample &s, char *buf, size_t len);

// Discrete changes between samples.
enum class ReplayEventType : uint8_t
{
  Boot = 0,
  Claim,        // a claim was made or changed
  Release,      // a claim was dropped
  RfidAuth,     // a card was accepted
  RfidDeauth,   // the authorisation ended (card tap, unplug or timeout)
  Limit,        // the session limit changed (value 0 / type none = cleared)
  Boost,        // a boost was armed (type none = cancelled / released)
  Schedule,     // the schedule was edited (the package holds the final one)
  Config,       // the configuration changed (the package holds the final one)
};

#define REPLAY_STATE_NONE      0
#define REPLAY_STATE_ACTIVE    1
#define REPLAY_STATE_DISABLED  2

struct ReplayEvent
{
  uint32_t up;            // seconds since boot
  uint32_t client;        // Claim / Release
  uint32_t value;         // Limit / Boost value
  int16_t  priority;      // Claim
  int16_t  charge_current;// Claim, -1 = not set
  int16_t  max_current;   // Claim, -1 = not set
  uint8_t  type;          // ReplayEventType
  uint8_t  state;         // Claim: REPLAY_STATE_*; Limit / Boost: limit type index
  uint8_t  auto_release;  // Claim / Limit
};

const char *replay_event_type_name(uint8_t type);
const char *replay_state_name(uint8_t state);

// A claim as the recorder last saw it.
struct ReplayClaim
{
  uint32_t client;
  int16_t  priority;
  int16_t  charge_current; // -1 = not set
  int16_t  max_current;    // -1 = not set
  uint8_t  state;          // REPLAY_STATE_*
  uint8_t  auto_release;
};

// Compare two claim sets and report each difference: `on_claim` for a claim
// that is new or changed in `cur`, `on_release` for a client present in
// `prev` and gone from `cur`. Returns the number of differences.
template <typename OnClaim, typename OnRelease>
size_t replay_diff_claims(const ReplayClaim *prev, size_t prev_count,
                          const ReplayClaim *cur, size_t cur_count,
                          OnClaim on_claim, OnRelease on_release)
{
  size_t changes = 0;
  for(size_t i = 0; i < cur_count; i++) {
    const ReplayClaim *before = nullptr;
    for(size_t j = 0; j < prev_count; j++) {
      if(prev[j].client == cur[i].client) {
        before = &prev[j];
        break;
      }
    }
    if(!before ||
       before->priority != cur[i].priority ||
       before->state != cur[i].state ||
       before->charge_current != cur[i].charge_current ||
       before->max_current != cur[i].max_current ||
       before->auto_release != cur[i].auto_release) {
      on_claim(cur[i]);
      changes++;
    }
  }
  for(size_t j = 0; j < prev_count; j++) {
    bool still = false;
    for(size_t i = 0; i < cur_count; i++) {
      if(cur[i].client == prev[j].client) {
        still = true;
        break;
      }
    }
    if(!still) {
      on_release(prev[j]);
      changes++;
    }
  }
  return changes;
}

#endif // REPLAY_FORMAT_H
