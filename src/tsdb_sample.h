#pragma once
#include <cstdint>

// Fixed column order for the energy tsdb. Do NOT reorder (on-disk layout).
enum {
  TSDB_COL_AMPS = 0,   // deci-amp   (A*10)
  TSDB_COL_VOLTS,      // volt
  TSDB_COL_POWER,      // deca-watt  (W/10)
  TSDB_COL_ENERGY,     // Wh delta since last sample
  TSDB_COL_TEMP,       // deci-degC  (C*10)
  TSDB_COL_SOC,        // percent 0..100, -1 = invalid
  TSDB_COL_PILOT,      // amp
  TSDB_NUM_COLS
};

extern const char *TSDB_PARAM_NAMES[TSDB_NUM_COLS];   // {"amps","volts",...}

struct EnergySample {
  double amps = 0;
  double volts = 0;
  double power_w = 0;
  double energy_wh_delta = 0;
  double temp_c = 0;
  int    soc = -1;        // -1 = no valid reading
  double pilot_a = 0;
};

// Scale a sample into TSDB_NUM_COLS int16 columns (clamped to int16 range).
void tsdb_scale_sample(const EnergySample &s, int16_t *out);
// Unscale one column index back to engineering units (double). SoC stays -1 if invalid.
double tsdb_unscale(uint8_t col, int16_t raw);
