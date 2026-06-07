# tsdb Energy-History Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `EnergyLogger`'s 6-hour raw store with an `esp_tsdb`-backed time-series DB on the core-3 16 MB boards only, giving ~100 days of minute-resolution history; 4 MB core-2 boards keep `EnergyLogger` unchanged.

**Architecture:** A build-gated (`ENABLE_TSDB`) path: a pure native-tested scaling unit converts an `EnergySample` (doubles) to/from 7 `int16_t` columns; a `TsdbEnergyLogger` MicroTask samples `EvseManager` once a minute and `tsdb_write()`s the row; the existing `/energy/*` routes become thin adapters over `tsdb_query_next()` / `tsdb_aggregate()`, with `monthly`/`annual` kept as persisted rollups. esp_tsdb is vendored (its `tigomonitor` branch) as an ESP-IDF component compiled only in core-3 envs.

**Tech Stack:** C++ (Arduino + PlatformIO), ESP-IDF ≥5.1 component (`esp_tsdb`, C), MicroTasks, LittleFS, ArduinoJson, doctest (native tests).

**Reference docs:** Design spec `docs/superpowers/specs/2026-06-05-tsdb-energy-history-design.md`. esp_tsdb API in `include/esp_tsdb.h` (vendored). Existing backend: `src/energy_logger.{h,cpp}`, `src/web_server_energy_logger.cpp`.

**Key API facts (from `esp_tsdb.h`):**
- `esp_err_t tsdb_init(const tsdb_config_t*)`; config fields: `filepath, num_params, param_names, max_records, index_stride, buffer_pool_size, alloc_strategy, use_paged_allocation, page_size`.
- `TSDB_CALC_MAX_RECORDS(storage_bytes, num_params)` = `((storage_bytes)-2048)/(4 + num_params*2)`.
- `alloc_strategy`: `TSDB_ALLOC_INTERNAL_RAM | TSDB_ALLOC_PSRAM | TSDB_ALLOC_AUTO`.
- `esp_err_t tsdb_write(uint32_t ts, const int16_t *values)`.
- `tsdb_query_init(tsdb_query_t*, uint32_t start, uint32_t end, const uint8_t *param_indices, uint8_t n)` (NULL indices = all); `tsdb_query_next(q, uint32_t *ts, int16_t *values)` returns `ESP_ERR_NOT_FOUND` at end; `tsdb_query_close(q)`.
- `tsdb_aggregate(uint32_t start, uint32_t end, uint8_t param_index, tsdb_agg_type_t, int32_t *result)`; types `TSDB_AGG_SUM/AVG/MIN/MAX/COUNT/FIRST/LAST`.
- `esp_err_t tsdb_get_stats(tsdb_stats_t*)`, `esp_err_t tsdb_query_count(start,end,uint32_t*)`.

**Column order (fixed), scales (from spec):**

| idx | name | scale (stored int16) | unscale |
|-----|------|----------------------|---------|
| 0 | `amps`   | `round(amps*10)`            | `/10.0` |
| 1 | `volts`  | `round(volts)`              | `*1`    |
| 2 | `power`  | `round(watts/10)`           | `*10`   |
| 3 | `energy` | `round(wh_delta)`           | `*1` (Wh) |
| 4 | `temp`   | `round(tempC*10)`           | `/10.0` |
| 5 | `soc`    | `soc` (0..100), `-1` invalid| `-1`→null |
| 6 | `pilot`  | `round(pilot_a)`            | `*1`    |

---

## Task 1: Vendor esp_tsdb as an IDF component (core-3 only), prove it builds

**Files:**
- Create: `components/esp_tsdb/` (vendored library, `tigomonitor` branch) OR add a git dependency entry — see Step 1.
- Modify: `src/idf_component.yml` (managed-dependency route) OR `platformio.ini` core-3 envs.

- [ ] **Step 1: Vendor the library**

Preferred (managed component, pins the branch). Append to `src/idf_component.yml`:

```yaml
dependencies:
  idf: '>=5.1'
  esp_tsdb:
    git: https://github.com/RAR/esp_tsdb.git
    version: tigomonitor
```

Fallback if the component manager cannot resolve a non-registry git dep in this build: vendor the
source directly into a project `components/esp_tsdb/` directory (copy `include/`, `src/`,
`CMakeLists.txt`, `idf_component.yml` from the `tigomonitor` checkout) and remove the git dep. The
component must be visible only to core-3 builds; `src/idf_component.yml` is already core-3-scoped in
this project, so no extra gating is needed for the dependency itself.

- [ ] **Step 2: Build the P4 env to confirm the component compiles and links**

Run: `pio run -e openevse_p4`
Expected: SUCCESS. The build log shows esp_tsdb sources compiling
(`tsdb_core.c`, `tsdb_write.c`, `tsdb_query.c`, …). No symbol is referenced yet, so nothing links it
into the image — that's fine; this step only proves the component resolves and compiles under IDF 5.

If the first core-3 build prints a transient `FRAMEWORK_DIR is None`, re-run the exact command once.

- [ ] **Step 3: Confirm the 4 MB core-2 build is unaffected**

Run: `pio run -e openevse_wifi_v1`
Expected: SUCCESS, byte-for-byte behavior unchanged (the component is not pulled into core-2 because
`src/idf_component.yml` only applies to the core-3/IDF build path).

- [ ] **Step 4: Commit**

```bash
git add src/idf_component.yml components/ 2>/dev/null
git commit -m "tsdb: vendor esp_tsdb (tigomonitor) as core-3 IDF component"
```

---

## Task 2: Pure scaling unit `tsdb_sample` + native tests (TDD)

**Files:**
- Create: `src/tsdb_sample.h`, `src/tsdb_sample.cpp`
- Create: `test/test_tsdb_sample/test_tsdb_sample.cpp`
- Modify: `platformio.ini` (`[env:native]` `build_src_filter` += `tsdb_sample.cpp`)

This unit is Arduino-free and esp_tsdb-free (just `<cstdint>`), so it compiles and tests on the host.

- [ ] **Step 1: Write the header**

`src/tsdb_sample.h`:

```cpp
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
```

- [ ] **Step 2: Write failing tests**

`test/test_tsdb_sample/test_tsdb_sample.cpp`:

```cpp
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
```

- [ ] **Step 3: Add native env source filter**

In `platformio.ini` `[env:native]`, add `tsdb_sample.cpp` to `build_src_filter` (alongside the existing
`+<ha_oauth.cpp> +<fake_evse_core.cpp>`): `+<tsdb_sample.cpp>`.

- [ ] **Step 4: Run tests to verify they fail**

Run: `pio test -e native -f test_tsdb_sample`
Expected: FAIL (link error — `tsdb_scale_sample` undefined).

- [ ] **Step 5: Implement `src/tsdb_sample.cpp`**

```cpp
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
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `pio test -e native -f test_tsdb_sample`
Expected: PASS, all cases.

- [ ] **Step 7: Commit**

```bash
git add src/tsdb_sample.h src/tsdb_sample.cpp test/test_tsdb_sample/ platformio.ini
git commit -m "tsdb: pure int16 scaling unit for energy samples + native tests"
```

---

## Task 3: `TsdbEnergyLogger` — init/config + 1-min write path (`ENABLE_TSDB`)

**Files:**
- Create: `src/tsdb_energy_logger.h`, `src/tsdb_energy_logger.cpp`
- Modify: `platformio.ini` (define `ENABLE_TSDB` for core-3 16 MB envs)
- Modify: `src/main.cpp` (wire whichever logger is active)
- Modify: `src/web_server_energy_logger.cpp` / `src/energy_logger.*` usage sites (exclude `EnergyLogger` when `ENABLE_TSDB`)

- [ ] **Step 1: Define `ENABLE_TSDB` on the core-3 16 MB envs**

In `platformio.ini`, add `-D ENABLE_TSDB` to `build_flags` of `openevse_p4`, `openevse_wifi_v1_16mb`,
and `openevse_wifi_v1_16mb_fake`. (Other core-3 envs like S3/TFT can opt in later; keep scope to the
two bench boards + P4 for now.)

- [ ] **Step 2: Write the header**

`src/tsdb_energy_logger.h`:

```cpp
#ifndef _TSDB_ENERGY_LOGGER_H
#define _TSDB_ENERGY_LOGGER_H
#ifdef ENABLE_TSDB
#include <Arduino.h>
#include <MicroTasks.h>
#include "evse_man.h"

#define TSDB_ENERGY_FILE          "/littlefs/energy.tsdb"
#define TSDB_ENERGY_SAMPLE_MS     60000                 // 1/min, matches EnergyLogger
#define TSDB_ENERGY_BYTES         (2500UL * 1024UL)     // ~2.5 MB -> ~100 days

class TsdbEnergyLogger : public MicroTasks::Task {
private:
  EvseManager *_evse = nullptr;
  bool         _ready = false;
  time_t       _last_sample = 0;
  double       _last_session_wh = 0;   // for per-sample energy delta
  bool         init_db();
protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);
public:
  void begin(EvseManager &evse);
  bool isReady() { return _ready; }
};

extern TsdbEnergyLogger tsdbEnergyLogger;
#endif
#endif
```

- [ ] **Step 3: Implement init/config + write (`src/tsdb_energy_logger.cpp`)**

```cpp
#ifdef ENABLE_TSDB
#include "tsdb_energy_logger.h"
#include "tsdb_sample.h"
#include "esp_tsdb.h"
#include "debug.h"

TsdbEnergyLogger tsdbEnergyLogger;

bool TsdbEnergyLogger::init_db() {
  tsdb_config_t cfg = {};
  cfg.filepath     = TSDB_ENERGY_FILE;
  cfg.num_params   = TSDB_NUM_COLS;
  cfg.param_names  = TSDB_PARAM_NAMES;
  cfg.max_records  = TSDB_CALC_MAX_RECORDS(TSDB_ENERGY_BYTES, TSDB_NUM_COLS);
  cfg.index_stride = 380;
#if defined(CONFIG_IDF_TARGET_ESP32P4)        // P4 has PSRAM
  cfg.alloc_strategy      = TSDB_ALLOC_PSRAM;
  cfg.buffer_pool_size    = 16 * 1024;
  cfg.use_paged_allocation= true;
  cfg.page_size           = 4096;
#else                                          // 16 MB WROOM-32E: no PSRAM
  cfg.alloc_strategy      = TSDB_ALLOC_INTERNAL_RAM;
  cfg.buffer_pool_size    = 10 * 1024;
  cfg.use_paged_allocation= true;
  cfg.page_size           = 2048;
#endif
  esp_err_t e = tsdb_init(&cfg);
  if (e != ESP_OK) { DBUGF("tsdb_init failed: %d", e); return false; }
  return true;
}

void TsdbEnergyLogger::begin(EvseManager &evse) { _evse = &evse; MicroTask.startTask(this); }

void TsdbEnergyLogger::setup() {
  _ready = init_db();
  _last_sample = time(NULL);
  _last_session_wh = _evse ? _evse->getSessionEnergy() : 0;
}

unsigned long TsdbEnergyLogger::loop(MicroTasks::WakeReason) {
  if (_ready && _evse) {
    EnergySample s;
    s.amps    = _evse->getAmps();
    s.volts   = _evse->getVoltage();
    s.power_w = s.amps * s.volts;
    double cur_wh = _evse->getSessionEnergy();
    s.energy_wh_delta = (cur_wh >= _last_session_wh) ? (cur_wh - _last_session_wh) : cur_wh;
    _last_session_wh  = cur_wh;
    s.temp_c  = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)
                  ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0;
    s.soc     = _evse->isVehicleStateOfChargeValid() ? _evse->getVehicleStateOfCharge() : -1;
    s.pilot_a = _evse->getPilot();

    int16_t row[TSDB_NUM_COLS];
    tsdb_scale_sample(s, row);
    esp_err_t e = tsdb_write((uint32_t)time(NULL), row);
    if (e != ESP_OK) DBUGF("tsdb_write failed: %d", e);
  }
  return TSDB_ENERGY_SAMPLE_MS;
}
#endif
```

> Note: confirm `EvseManager` getter names against `src/evse_man.h` / `src/evse_monitor.h`
> (`getAmps`, `getVoltage`, `getSessionEnergy`, `getTemperature`, `isTemperatureValid`,
> `getVehicleStateOfCharge`, `isVehicleStateOfChargeValid`, `getPilot`) — these mirror what
> `EnergyLogger::take_sample` already uses.

- [ ] **Step 4: Gate `EnergyLogger` out and wire `TsdbEnergyLogger` in `main.cpp`**

Find the `EnergyLogger` instance/`begin()` in `main.cpp` (and its global in `energy_logger.cpp`).
Wrap so exactly one logger is active:

```cpp
#ifdef ENABLE_TSDB
  tsdbEnergyLogger.begin(evse);
#else
  energyLogger.begin(evse);     // existing call, unchanged
#endif
```

Guard the `EnergyLogger energyLogger;` global definition and its `#include` with `#ifndef ENABLE_TSDB`
so it is not compiled on the tsdb path. Do the same for any other `energyLogger.` references in
`main.cpp` / loop.

- [ ] **Step 5: Build P4 and confirm init**

Run: `pio run -e openevse_p4`
Expected: SUCCESS, image links esp_tsdb now. (Runtime init verified on HW in Task 7.)

- [ ] **Step 6: Build core-2 regression**

Run: `pio run -e openevse_wifi_v1`
Expected: SUCCESS, `EnergyLogger` still compiled (ENABLE_TSDB undefined), no behavior change.

- [ ] **Step 7: Commit**

```bash
git add src/tsdb_energy_logger.h src/tsdb_energy_logger.cpp src/main.cpp platformio.ini
git commit -m "tsdb: TsdbEnergyLogger init + 1-min write path, gated on ENABLE_TSDB"
```

---

## Task 4: `/energy/raw` adapter over tsdb (`ENABLE_TSDB`)

**Files:**
- Modify: `src/web_server_energy_logger.cpp` (`handleEnergyRaw`, under `#ifdef ENABLE_TSDB`)

Goal: same JSON array shape the GUI consumes today, sourced from a tsdb range query.

- [ ] **Step 1: Inspect the existing `/energy/raw` JSON shape**

Read the current `handleEnergyRaw` (and `web_server.cpp:1332` route registration) and note the exact
field names/order the GUI's `clipToSession`/`SessionChart` expects (`timestamp`, `amps`,
`temperature`, `energy_wh`, `soc`). Record the existing query params (`reldataref`/range) it honors.

- [ ] **Step 2: Implement the tsdb-backed handler**

Under `#ifdef ENABLE_TSDB`, replace the body of `handleEnergyRaw` with a streamed range query. Default
window = last `ENERGY_LOGGER_RAW_KEEP_HOURS`-equivalent (e.g. last 3 h) unless a range is supplied, to
preserve current dashboard behavior:

```cpp
uint32_t now = (uint32_t)time(NULL);
uint32_t start = now - 3*3600;          // honor existing range params if present
tsdb_query_t q;
if (tsdb_query_init(&q, start, now, NULL, TSDB_NUM_COLS) != ESP_OK) { /* 200 [] */ }
// stream: "[" then {"timestamp":ts,"amps":..,"temperature":..,"energy_wh":..,"soc":..} ,...
uint32_t ts; int16_t v[TSDB_NUM_COLS];
bool first = true;
while (tsdb_query_next(&q, &ts, v) == ESP_OK) {
  // unscale v[] via tsdb_unscale(col, v[col]); soc==-1 -> omit or null per current shape
  // append JSON object (comma-separated)
}
tsdb_query_close(&q);
// "]"
```

Keep the existing additive fields too (`volts`, `power`, `pilot`) — older GUI ignores unknown keys.
Match the current content-type and chunked/streaming approach used elsewhere in the file.

- [ ] **Step 3: Build P4**

Run: `pio run -e openevse_p4`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/web_server_energy_logger.cpp
git commit -m "tsdb: serve /energy/raw from tsdb range query (ENABLE_TSDB)"
```

---

## Task 5: `/energy/daily` + `/energy/weekly` from `tsdb_aggregate` (`ENABLE_TSDB`)

**Files:**
- Modify: `src/web_server_energy_logger.cpp` (`handleEnergyDaily`, add `handleEnergyWeekly`)
- Modify: `src/web_server.cpp` (register `/energy/weekly$` route under `#ifdef ENABLE_TSDB`)

- [ ] **Step 1: Note today's `/energy/daily` JSON shape**

Read existing `handleEnergyDaily` output (date, `energy_wh`, `peak_temp`, `min_temp`) so the aggregated
version matches.

- [ ] **Step 2: Implement daily aggregation**

For each day bucket `[d0, d1)` within the window (default last 30 days):
```cpp
int32_t wh, tmax, tmin;
tsdb_aggregate(d0, d1, TSDB_COL_ENERGY, TSDB_AGG_SUM, &wh);
tsdb_aggregate(d0, d1, TSDB_COL_TEMP,   TSDB_AGG_MAX, &tmax);
tsdb_aggregate(d0, d1, TSDB_COL_TEMP,   TSDB_AGG_MIN, &tmin);
// emit {"date":"YYYY-MM-DD","energy_wh":wh,"peak_temp":tmax/10.0,"min_temp":tmin/10.0}
```
Use `localtime_r`/`mktime` for bucket boundaries (mirror EnergyLogger's date math). Skip empty buckets
(use `tsdb_query_count` or `TSDB_AGG_COUNT==0`).

- [ ] **Step 3: Implement `/energy/weekly`**

Same as daily but 7-day buckets (last ~12 weeks). Register `server.on("/energy/weekly$", handleEnergyWeekly);`
under `#ifdef ENABLE_TSDB`.

- [ ] **Step 4: Build P4**

Run: `pio run -e openevse_p4`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/web_server_energy_logger.cpp src/web_server.cpp
git commit -m "tsdb: derive /energy/daily + /energy/weekly via tsdb_aggregate (ENABLE_TSDB)"
```

---

## Task 6: Keep `monthly`/`annual` as persisted daily-fed rollups (`ENABLE_TSDB`)

**Files:**
- Modify: `src/tsdb_energy_logger.cpp` (add a once-per-day rollup writer)
- Modify: `src/web_server_energy_logger.cpp` (`handleEnergyMonthly`, `handleEnergyAnnual` read the files)

Rationale: the ~100-day tsdb window cannot serve a 12-month or multi-year panel, so monthly/annual stay
as small persisted JSON files, appended once per day.

- [ ] **Step 1: Add a daily rollup tick in `TsdbEnergyLogger::loop`**

When the local date rolls over, compute the previous day's total energy (`tsdb_aggregate(SUM)` over the
day) and peak/min temp, then append/update `/littlefs/monthly.json` (≤12 entries/yr per month) and
`/littlefs/annual.json` (one entry/yr). Reuse the existing `MonthlyMetrics`/`AnnualMetrics`
serialization from `energy_logger.h` (move those structs to a shared header if needed, or duplicate the
tiny serialize/deserialize — prefer sharing).

- [ ] **Step 2: Point monthly/annual handlers at the files**

Under `#ifdef ENABLE_TSDB`, `handleEnergyMonthly`/`handleEnergyAnnual` read and stream
`/littlefs/monthly.json` / `/littlefs/annual.json` (same JSON the GUI expects today). If a file is
missing, return `[]`.

- [ ] **Step 3: Build P4**

Run: `pio run -e openevse_p4`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/tsdb_energy_logger.cpp src/web_server_energy_logger.cpp src/energy_logger.h
git commit -m "tsdb: persist monthly/annual rollups (daily-fed) beyond the tsdb window"
```

---

## Task 7: Full build + hardware validation (P4 + 16 MB WROOM + core-2 regression)

**Files:** none (validation only). Bench: P4 `/dev/ttyACM5` (10.75.1.143), 16 MB WROOM `/dev/ttyUSB0` (10.75.0.28). Build GUI with `GUI_NAME=gui-nightshift`; restore `web_static` after (`git checkout -- src/web_static && git clean -fdq src/web_static`).

- [ ] **Step 1: Native tests still pass**

Run: `pio test -e native`
Expected: all suites PASS (incl. `test_tsdb_sample`).

- [ ] **Step 2: core-2 regression build**

Run: `pio run -e openevse_wifi_v1`
Expected: SUCCESS; EnergyLogger path unchanged.

- [ ] **Step 3: Flash P4 (+ FakeEVSE) and confirm tsdb init + write**

```bash
GUI_NAME=gui-nightshift PLATFORMIO_BUILD_FLAGS=-DFAKE_EVSE \
  pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5
```
Drive a charge (FakeEVSE: `vehicle:1` + `/override active`), wait a few minutes, then:
`curl -s 'http://10.75.1.143/energy/raw' | head` — expect samples with correct unscaled
`amps/volts/temperature/energy_wh/soc`.

- [ ] **Step 4: Force a ring wrap and verify queries across the wrap**

Temporarily shrink `TSDB_ENERGY_BYTES` (or use a debug build constant) so `max_records` is small (e.g.
a few hundred), drive FakeEVSE at high sample rate until the ring wraps, then query a sub-range and a
full range:
`curl -s 'http://10.75.1.143/energy/raw?...'` and `/energy/daily`.
Expected: results are non-empty, time-ascending, and include the newest records (this is the exact
failure the `tigomonitor` patch fixes). Restore `TSDB_ENERGY_BYTES` after.

- [ ] **Step 5: Verify aggregates + rollups + GUI**

`curl -s http://10.75.1.143/energy/daily` and `/energy/weekly` return bucketed totals; `/energy/monthly`
+ `/energy/annual` return (initially empty `[]`, then populated after a simulated day rollover). Load the
nightshift dashboard and confirm the session/history chart renders from `/energy/raw`.

- [ ] **Step 6: Flash 16 MB WROOM (no PSRAM path)**

```bash
GUI_NAME=gui-nightshift pio run -e openevse_wifi_v1_16mb_fake -t upload --upload-port /dev/ttyUSB0
```
Confirm `tsdb_init` succeeds on the internal-RAM/paged path (no PSRAM), `/energy/raw` works, and free
heap stays healthy (`/status` `free_heap`). Restore `web_static`.

- [ ] **Step 7: Final commit / branch finish**

```bash
git checkout -- src/web_static && git clean -fdq src/web_static
git add -A && git commit -m "tsdb: hardware validation notes (P4 + 16MB WROOM)"
```
Then use **superpowers:finishing-a-development-branch** to wrap up.

---

## Open items / confirm during implementation

- **Exact `EvseManager` getter names** for amps/volts/session-energy/temp/soc/pilot (Task 3 Step 3) —
  verify against `src/evse_man.h`; they mirror `EnergyLogger::take_sample`.
- **`tsdb_config_t` zero-init**: confirm unused fields (e.g. when not paged) are safe at 0.
- **`/energy/raw` range params**: match whatever `reldataref`/range the current handler + GUI use, so
  default behavior is unchanged.
- **monthly/annual feed source**: prefer `EnergyMeter` daily totals if cleaner than re-aggregating the
  tsdb day; pick one and note it.
