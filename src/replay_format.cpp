#include "replay_format.h"

#include <math.h>
#include <stdio.h>

const char *const REPLAY_SAMPLE_COLUMNS[] = {
  "up", "solar_w", "grid_ie_w", "live_pwr_w", "volts", "amps", "temp_c",
  "session_wh", "pilot", "state", "soc", "target_current", "flags",
};
const size_t REPLAY_SAMPLE_COLUMN_COUNT =
  sizeof(REPLAY_SAMPLE_COLUMNS) / sizeof(REPLAY_SAMPLE_COLUMNS[0]);

static long clampl(double v, long lo, long hi)
{
  if(isnan(v)) {
    return 0;
  }
  long r = lround(v);
  return r < lo ? lo : (r > hi ? hi : r);
}

void replay_sample_set(ReplaySample &s, uint32_t up, double solar_w, double grid_ie_w,
                       double live_pwr_w, double volts, double amps, bool temp_valid,
                       double temp_c, double session_wh, long pilot, uint8_t evse_state,
                       int soc, uint32_t target_current, uint8_t flags)
{
  s.up = up;
  s.solar_dw = (int16_t)clampl(solar_w / 10.0, INT16_MIN, INT16_MAX);
  s.grid_ie_dw = (int16_t)clampl(grid_ie_w / 10.0, INT16_MIN, INT16_MAX);
  s.live_pwr_dw = (int16_t)clampl(live_pwr_w / 10.0, INT16_MIN, INT16_MAX);
  s.volts_dv = (uint16_t)clampl(volts * 10.0, 0, UINT16_MAX);
  s.amps_ca = (uint16_t)clampl(amps * 100.0, 0, UINT16_MAX);
  s.temp_dc = temp_valid ? (int16_t)clampl(temp_c * 10.0, INT16_MIN + 1, INT16_MAX)
                         : REPLAY_TEMP_INVALID;
  s.session_dwh = (uint16_t)clampl(session_wh / 10.0, 0, UINT16_MAX);
  s.pilot = (uint8_t)clampl((double)pilot, 0, UINT8_MAX);
  s.evse_state = evse_state;
  s.soc = (soc < 0 || soc > 100) ? REPLAY_SOC_INVALID : (int8_t)soc;
  s.target_current = (uint8_t)(target_current > UINT8_MAX ? UINT8_MAX : target_current);
  s.flags = flags;
}

size_t replay_sample_row(const ReplaySample &s, char *buf, size_t len)
{
  char temp[16];
  if(REPLAY_TEMP_INVALID == s.temp_dc) {
    snprintf(temp, sizeof(temp), "null");
  } else {
    snprintf(temp, sizeof(temp), "%.1f", s.temp_dc / 10.0);
  }
  char soc[8];
  if(REPLAY_SOC_INVALID == s.soc) {
    snprintf(soc, sizeof(soc), "null");
  } else {
    snprintf(soc, sizeof(soc), "%d", (int)s.soc);
  }
  int n = snprintf(buf, len, "[%lu,%d,%d,%d,%.1f,%.2f,%s,%u,%u,%u,%s,%u,%u]",
                   (unsigned long)s.up,
                   s.solar_dw * 10, s.grid_ie_dw * 10, s.live_pwr_dw * 10,
                   s.volts_dv / 10.0, s.amps_ca / 100.0, temp,
                   (unsigned)s.session_dwh * 10u, (unsigned)s.pilot, (unsigned)s.evse_state,
                   soc, (unsigned)s.target_current, (unsigned)s.flags);
  if(n < 0 || (size_t)n >= len) {
    return 0;
  }
  return (size_t)n;
}

const char *replay_event_type_name(uint8_t type)
{
  static const char *const names[] = {
    "boot", "claim", "release", "rfid_auth", "rfid_deauth", "limit", "boost",
    "schedule", "config",
  };
  return type < sizeof(names) / sizeof(names[0]) ? names[type] : "unknown";
}

const char *replay_state_name(uint8_t state)
{
  switch(state) {
    case REPLAY_STATE_ACTIVE:   return "active";
    case REPLAY_STATE_DISABLED: return "disabled";
    default:                    return "none";
  }
}
