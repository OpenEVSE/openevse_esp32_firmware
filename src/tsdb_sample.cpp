#include "tsdb_sample.h"
#include <cmath>

const char *TSDB_PARAM_NAMES[TSDB_NUM_COLS] =
  {"amps","volts","power","energy","temp","soc","pilot"};

static int16_t clamp16(double v) {
  if (v >  32767.0) return  32767;
  if (v < -32768.0) return -32768;
  return (int16_t)llround(v);
}

void tsdb_scale_sample(const EnergySample &s, int16_t *o) {
  o[TSDB_COL_AMPS]   = clamp16(s.amps * 10.0);
  o[TSDB_COL_VOLTS]  = clamp16(s.volts);
  o[TSDB_COL_POWER]  = clamp16(s.power_w / 10.0);
  o[TSDB_COL_ENERGY] = clamp16(s.energy_wh_delta);
  o[TSDB_COL_TEMP]   = clamp16(s.temp_c * 10.0);
  o[TSDB_COL_SOC]    = (s.soc < 0) ? -1 : clamp16((double)s.soc);
  o[TSDB_COL_PILOT]  = clamp16(s.pilot_a);
}

double tsdb_unscale(uint8_t col, int16_t raw) {
  switch (col) {
    case TSDB_COL_AMPS:  return raw / 10.0;
    case TSDB_COL_POWER: return raw * 10.0;
    case TSDB_COL_TEMP:  return raw / 10.0;
    case TSDB_COL_SOC:   return (raw < 0) ? -1 : (double)raw;
    default:             return (double)raw;   // volts, energy, pilot
  }
}
