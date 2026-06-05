# tsdb Energy-History Backend (P4 / 16 MB only) — Design

**Date:** 2026-06-05
**Status:** Implemented + HW-validated on `feature/tsdb-energy-history` (P4 **and** 16 MB WROOM).
**Branch target:** `feature/esp32-modernization` (new work branch `feature/tsdb-energy-history`)

> **HW validation outcome (2026-06-05):** Validated on **both** targets — the
> **ESP32-P4** (riscv + PSRAM) and the **16 MB WROOM-32E** (xtensa, internal RAM):
> init, 1-min write, `/energy/raw`, daily/weekly aggregation, and a forced
> **ring-wrap** query test all pass; the boards survive sustained write cycles.
>
> Validation found a real **esp_tsdb bug** on the xtensa/internal-RAM path: its
> `TSDB_ALLOC_INTERNAL_RAM` strategy allocated block buffers with
> `MALLOC_CAP_INTERNAL` only, which on ESP32 can return **IRAM** (32-bit-word
> access only). The block writer does int16 (halfword) stores, so the 60-s sample
> write faulted with `LoadStoreError` → reboot-loop. (riscv + the PSRAM path are
> byte-addressable, so the P4 never hit it.) **Fixed** to
> `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` in
> `components/esp_tsdb/src/tsdb_buffer.c` — worth pushing to the upstream
> esp_tsdb `tigomonitor` branch since it affects any xtensa/internal-RAM user.

## Goal

Replace the on-device energy time-series store on the **core-3 16 MB boards only**
(`openevse_p4`, `openevse_wifi_v1_16mb*`) with [`esp_tsdb`](https://github.com/RAR/esp_tsdb/tree/tigomonitor),
a proper columnar time-series database. This turns today's flash-starved **6 hours of
minute-resolution raw** (`EnergyLogger`) into **~3–4 months** of full-resolution minute data, and
replaces the hand-rolled rollup/eviction tiers with the library's ring-buffer eviction and built-in
aggregations.

The 4 MB core-2 fleet (`openevse_wifi_v1`, etc.) is **untouched** — it keeps `EnergyLogger`. This is
a strictly additive, build-gated change.

## Background / why this approach

The current backend, `EnergyLogger` (`src/energy_logger.{h,cpp}`, served by
`src/web_server_energy_logger.cpp`), is a four-tier rollup capped at **~20 KB total** across all
`/logs` files — a design forced entirely by the tiny 4 MB LittleFS budget:

- Raw: 180-sample RAM ring (3 h) + 3 h chunk files in `/logs/raw/`, **6 h kept**.
- Daily: `/logs/daily/YYYY-QN.json` (quarterly), ~6 months.
- Monthly: `/logs/monthly/YYYY.json`.
- Annual: `/logs/annual.json`.

This is separate from `EnergyMeter` (`emeter.json`), which holds the running session/daily/…/yearly
**kWh totals** and `session_elapsed`. The logger is the *time series*; the meter is the *accumulators*.
This design touches only the logger.

`esp_tsdb` is a data-agnostic columnar TSDB for ESP32 (IDF ≥ 5.0.0): `N × int16_t` + timestamp per
record, sparse binary-search time index, ring-buffer LRU eviction, streaming zero-copy queries, and
built-in `SUM/AVG/MIN/MAX/COUNT/FIRST/LAST`. It allocates its working buffer from internal RAM or
PSRAM (with paged allocation for fragmented heaps). ESP32-P4 is a declared target.

**Why P4/16-only:** `esp_tsdb` requires **IDF ≥ 5.0.0**, available only on the core-3
(`platform_core3` / pioarduino) envs. The 4 MB mainline boards run `espressif32@6.12.0`
(arduino-esp32 2.x / IDF 4.4) and cannot build it. The core-3 build already uses the IDF component
manager (`src/idf_component.yml` pins `idf >=5.1`), so vendoring a component is a paved path there.

**Why vendor the `tigomonitor` branch, not released `v2.0.3`:** that branch carries a single
load-bearing fix — *"correct time-range iteration on a wrapped ring buffer"* (`tsdb_query.c`). Without
it, once the ring fills and starts evicting, range queries return wrong/empty results (day/week panels
go to 0 after wrap). Continuous minute-logging on an EVSE reaches that wrapped steady state, so the fix
is mandatory for this use case.

## Non-goals / out of scope

- **No change to the 4 MB core-2 boards.** They keep `EnergyLogger` unchanged.
- **No repartition.** `openevse_16mb.csv` is left as-is to avoid breaking OTA on fielded units. The
  ~100-day window is a consequence of the existing 3.375 MB LittleFS budget, accepted deliberately.
- **No GUI change.** All `/energy/*` endpoints keep their current JSON shape; nightshift is untouched.
- **No migration of existing `/logs` data** into tsdb. The old store is abandoned on the tsdb path
  (bench tool; history simply restarts). `EnergyMeter` totals are unaffected.

## Architecture

```
                         (ENABLE_TSDB / core-3 16 MB only)
  EvseManager ──sample──▶ TsdbEnergyLogger ──tsdb_write(ts,int16[7])──▶ esp_tsdb
       │  (1/min)              │  scale doubles -> int16                  │ /littlefs/energy.tsdb
       │                       │                                          │ ring buffer, LRU evict
  /energy/raw      ◀───────────┤  adapter: tsdb_query_next -> JSON        │
  /energy/daily    ◀───────────┤  adapter: tsdb_aggregate (day buckets)   │
  /energy/weekly   ◀───────────┤  adapter: tsdb_aggregate (week buckets)  │
  /energy/monthly  ◀── persisted rollup (monthly.json, daily-fed)         │
  /energy/annual   ◀── persisted rollup (annual.json,  daily-fed)         │
```

On the 4 MB / non-`ENABLE_TSDB` build the diagram is unchanged from today: `EnergyLogger` serves all
`/energy/*` routes.

### Components

1. **Vendored `esp_tsdb`** — local IDF component (`components/esp_tsdb/`) tracking the `tigomonitor`
   branch. Compiled only in core-3 envs. Plain C, IDF component-manager integrated.

2. **`tsdb_sample` scaling (pure, native-testable)** — a small Arduino-free unit that converts an
   `EnergySample` of doubles into the 7 `int16_t` columns and back, with documented fixed-point
   scales and the SoC `-1` sentinel. Lives in its own `.cpp` so it compiles under `pio test -e native`
   (mirrors `fake_evse_core` / `ha_*` pure cores).

3. **`TsdbEnergyLogger` (`src/tsdb_energy_logger.{h,cpp}`, `#ifdef ENABLE_TSDB`)** — a
   `MicroTasks::Task` that samples `EvseManager` once per minute (same cadence as `EnergyLogger`),
   builds the int16 row via the scaling unit, and calls `tsdb_write()`. Owns `tsdb_init()`/config and
   the daily-fed `monthly.json` / `annual.json` rollup writers.

4. **`/energy/*` adapters** — extend/replace `web_server_energy_logger.cpp` handlers (under
   `#ifdef ENABLE_TSDB`) so `/energy/raw` streams from `tsdb_query_next()` and `/energy/daily` +
   `/energy/weekly` come from `tsdb_aggregate()`, all serialized to today's JSON shape.
   `/energy/monthly` + `/energy/annual` read the persisted rollup files.

5. **Build gating** — `ENABLE_TSDB` defined for the core-3 16 MB envs in `platformio.ini`. The
   `EnergyLogger` instance and its handlers are compiled only when `ENABLE_TSDB` is *not* set; the
   tsdb path compiled only when it *is*. `main.cpp` wires whichever is active.

## Schema (7 columns, int16, fixed-point)

`int16_t` is signed (−32768..32767). Scales chosen so realistic EVSE ranges never overflow and the
column count (7) stays clear of the library's `<11` / `>38` block-geometry boundaries (CHANGELOG
2.0.1).

| # | column | source | stored as | scale | worst-case range |
|---|--------|--------|-----------|-------|------------------|
| 0 | amps   | `evse.getAmps()`                         | deci-amp | ×10 | 80 A → 800 ✓ |
| 1 | volts  | `evse.getVoltage()`                      | volt     | ×1  | ≤300 ✓ |
| 2 | power  | amps × volts (× phases)                  | deca-watt| ÷10 | 3φ·80 A·240 V ≈ 57.6 kW → 5760 ✓ (W would overflow >32.7 kW) |
| 3 | energy | per-sample Wh delta                      | Wh       | ×1  | ≤~960 Wh/min ✓ |
| 4 | temp   | `getTemperature(EVSE_MONITOR_TEMP_MONITOR)` | deci-°C | ×10 | ±3276 °C ✓ |
| 5 | soc    | `getVehicleStateOfCharge()`              | percent  | ×1, **−1 = invalid** | 0..100 ✓ |
| 6 | pilot  | `getPilot()`                             | amp      | ×1  | 0..80 ✓ |

Column order is fixed and recorded in `param_names[]`. `energy` is a **per-sample delta** (Wh since the
last sample), not a cumulative total, so it never overflows and `tsdb_aggregate(SUM)` over a range
gives total Wh for that range.

## Storage & memory (per-board)

- **File:** `/littlefs/energy.tsdb`. `max_records` sized for **~2.5 MB** of data
  (`TSDB_CALC_MAX_RECORDS(2_500_000, 7)`), ≈ **100 days at 1 sample/min**. Leaves ~0.7 MB of the
  3.375 MB LittleFS for config, certs, `monthly.json`, `annual.json`, and future use. Ring-buffer LRU
  evicts the oldest record once full.
- **Buffer allocation:**
  - **P4** (32 MB PSRAM): PSRAM allocation strategy (`MALLOC_CAP_SPIRAM` path) with a generous pool
    (e.g. 10–16 KB), paged allocation enabled. (Exact enum name confirmed against `esp_tsdb.h` at
    implementation time.)
  - **16 MB WROOM-32E** (**no PSRAM**): internal RAM only, small pool (~10 KB), `use_paged_allocation`
    true with a modest `page_size` to tolerate heap fragmentation.
  - The per-board config differences are isolated to the `tsdb_config_t` built at `begin()` (selected
    by a board macro), nothing else.

## Read path — endpoints unchanged

All routes keep today's JSON shape so the nightshift GUI needs **zero changes**:

- **`/energy/raw`** (optionally `?range`/`?reldataref` as today) → `tsdb_query_init` + loop
  `tsdb_query_next()`, unscaling each row back to the current JSON fields (`timestamp`, `amps`,
  `temperature`, `energy_wh`, `soc`, plus the new `volts`/`power`/`pilot` if the GUI wants them; extra
  fields are additive and ignored by older GUI). Streamed to the response, no full result buffering.
- **`/energy/daily`**, **`/energy/weekly`** → `tsdb_aggregate()` per day/week bucket within the window:
  `SUM` for energy, `MIN`/`MAX` for temp, matching the existing daily JSON.
- **`/energy/monthly`**, **`/energy/annual`** → read the persisted `monthly.json` / `annual.json`.
  These outlive the ~100-day tsdb window, so they remain daily-fed rollup files (a once-per-day job in
  `TsdbEnergyLogger` appends the day's totals, same idea as today's rollup but sourced from the meter /
  daily aggregate).

## Error handling

- **tsdb open/init failure** (corrupt file, no space): log, attempt the library's header
  reconstruction; if still failing, recreate the file empty (history restarts) rather than blocking
  boot. Charging/UI never depend on the logger.
- **LittleFS full:** the ring buffer is fixed-size and pre-sized, so the tsdb file does not grow at
  runtime; rollup files are bounded. No unbounded-growth path.
- **Out-of-range sample:** the scaling unit clamps to int16 limits (defensive; ranges above show this
  shouldn't trigger) and logs once.
- **Query across wrap:** handled by the vendored `tigomonitor` fix; covered explicitly by tests.

## Testing

- **Native (`pio test -e native`):** doctest suite for the `tsdb_sample` scaling unit — round-trip
  scale/unscale for each column, the SoC `-1` sentinel, the power/energy overflow guards, and the
  aggregate-bucket boundary math (pure, no Arduino, no tsdb). `build_src_filter` includes only the
  scaling `.cpp`.
- **esp_tsdb's own `host_test/`** harness validates the engine (incl. the wrap fix) on the host.
- **On-device (P4 + 16 MB WROOM):**
  1. Confirm `tsdb_init` succeeds and the file is created in LittleFS.
  2. Drive samples (FakeEVSE can emit fast) and verify `/energy/raw` returns them with correct
     unscaled values and the GUI chart renders.
  3. **Force a ring wrap** (FakeEVSE high-rate writes until `max_records` exceeded) and verify range
     queries across the wrap return correct, monotonic, non-empty results — the exact failure the
     `tigomonitor` patch fixes.
  4. Verify `/energy/daily` + `/energy/weekly` aggregates and the persisted `monthly`/`annual` rollups.
  5. Confirm the 4 MB core-2 build still builds and behaves exactly as before (EnergyLogger path).
