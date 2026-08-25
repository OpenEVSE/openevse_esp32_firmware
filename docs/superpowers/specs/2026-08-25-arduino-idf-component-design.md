# Arduino as an ESP-IDF Component — Design Spec

**Date:** 2026-08-25
**Status:** Approved for planning
**Branch:** `feature/arduino-idf-component` off `master`
**Author:** Andrew Rankin

## Goal

Migrate all core-3 build environments from Espressif's prebuilt Arduino
archives to pioarduino's hybrid `framework = arduino, espidf` mode, where
ESP-IDF is built from source with the Arduino core as an ordinary IDF
component. This keeps the Arduino API unchanged and makes sdkconfig a real,
Kconfig-resolved input instead of the current `custom_sdkconfig` shim.

## Why

1. **Silent sdkconfig failure class.** The prebuilt-libs shim cannot carry
   CMake INTERFACE link flags. `MBEDTLS_DYNAMIC_BUFFER` shipped as dead code
   until 12 hand-maintained `-Wl,--wrap=mbedtls_ssl_*` flags were added to
   `platformio.ini`; that list silently rots when IDF adds a 13th. In hybrid
   mode, `espidf.py` reads link args from the CMake File API reply
   (`extract_link_args()`), the exact channel components publish INTERFACE
   flags through — they propagate automatically.
2. **`custom_sdkconfig` operational mess.** The shim rebuilds IDF libs
   in place in the shared framework package and `check_reinstall_frwrk()`
   wipes/re-downloads the stock framework whenever an env *without*
   `custom_sdkconfig` is built. Alternating envs (e.g. TFT then 16MB)
   thrashes reinstall → rebuild → reinstall; unworkable in CI. This is the
   main reason the AWS-IoT TLS build is not upstreamable today.
3. **Kconfig dependency resolution.** In the shim, options whose `depends on`
   is unsatisfied vanish silently (e.g. `MBEDTLS_DYNAMIC_BUFFER` vs DTLS) and
   nonexistent options are accepted. Real Kconfig fixes the first case — an
   existing symbol's dependencies are resolved for real, whatever it `select`s
   comes along automatically, and the outcome is visible in the generated
   `sdkconfig.<env>`. It does **not** fix the second: *(negative-tested
   2026-08-25 — nonexistent symbols still drop silently; adding
   `CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT=y`, which does not exist in IDF
   5.5.4, built SUCCESS with no warning anywhere in the log.)* Verify an
   option landed by grepping the generated `sdkconfig.<env>`, never by a
   zero exit code.
4. **RAM knobs (measure now, flip later).** From-source IDF unlocks options
   the prebuilt config locks: `CONFIG_BT_ENABLED=y` (4,749 B DRAM .bss in BT
   symbols, no `esp_bt_controller_mem_release` caller — verify at runtime
   whether the region is already heap-reclaimed), `CONFIG_SPIRAM=y` on the
   no-PSRAM TFT board, `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8`, full CA
   bundle. **Flipping these is out of scope** — this migration only makes
   them measurable and flippable.

**This does NOT fix heap fragmentation.** `heap_largest` collapse under load
is an allocation-pattern problem; no sdkconfig value touches it.

## Verified platform facts (2026-08-25, pioarduino 55.03.311 & 55.03.38)

- `platform.json` frameworks = `['arduino', 'espidf']`; `framework-espidf`
  v5.5.5 is pinned by the platform and matches Arduino core 3.3.11's IDF —
  version parity is the platform's job, not ours.
- Hybrid split: the CMake/ninja pass builds IDF + the Arduino core only.
  `lib_deps` and Arduino built-in libraries (`LIBSOURCE_DIRS` points at the
  Arduino `libraries/` dir) are compiled by the normal SCons/LDF pass and
  linked using args extracted from the CMake File API. Our ~12 Arduino-API
  libraries never enter CMake.
- `espidf.py` auto-generates minimal root and `src/` `CMakeLists.txt` when
  absent; sdkconfig output is per-env (`sdkconfig.<PIOENV>`), seeded from
  `sdkconfig.defaults`; `custom_sdkconfig` is still honored in hybrid mode
  and lands in a real Kconfig-resolved `sdkconfig.defaults`.
- An old `#framework = arduino, espidf` line already sits commented in
  `[env]` — a previous attempt exists; expect no config landmines but no
  guarantees either.

## Scope

**Migrate:** every core-3 env present on the implementation branch (off
`master`): `openevse_wifi_tft_v1`, `openevse_wifi_tft_v1_dev`,
`openevse_wifi_v1_16mb`. The remaining core-3 envs
(`openevse_wifi_v1_16mb_fake`, `openevse_s3_lcd4`, `openevse_s3_lcd43b`,
`openevse_p4`) live on other fork branches and adopt the same shared
pattern when they rebase onto this work.

**Untouched:** every core-2 (espressif32@6.12.0) 4MB env, `native`,
partition tables, board manifests, all application source.

## Phase 0 — Feasibility spike (go/no-go gate)

Throwaway build of `openevse_wifi_tft_v1` changed only by
`framework = arduino, espidf`. Answers, in order:

1. **Lib resolution:** do all `lib_deps` (ArduinoJson, ArduinoMongoose,
   MicroDebug, ConfigJson, OpenEVSE, ESPAL, StreamSpy, MicroTasks,
   MicroOcpp ×2, LVGL, display/LED libs) compile via LDF and link clean?
2. **sdkconfig seeding:** is the initial `sdkconfig.defaults` seeded from
   Arduino's shipped per-chip config (behavior parity with prebuilt), or do
   we hand-seed? Diff the resolved `sdkconfig.openevse_wifi_tft_v1` against
   the prebuilt Arduino sdkconfig; every delta must be explained.
3. **The --wrap proof:** with `MBEDTLS_DYNAMIC_BUFFER=y` (+ its DTLS-off
   dependency) set via sdkconfig and **zero** hand `--wrap` flags, verify in
   the artifact: `nm firmware.elf | grep __wrap_mbedtls_ssl_read` and the
   map file show the wrap symbols referenced. The artifact, not the config,
   is the evidence.
4. **Cost:** clean and incremental build times; image-size delta vs the
   prebuilt build (must still fit the 16MB layout's app slot).
5. **Boot smoke:** flash the bench unit (10.75.1.162 only — never the live
   unit); boot, WiFi, web UI reachable.

**Go** = 1, 3, 5 pass and 2's deltas are explainable. **No-go** = report
findings, stop; the branch dies without touching other envs.

## Migration design (post-spike)

- **CMakeLists:** commit hand-written minimal root `CMakeLists.txt` and
  `src/CMakeLists.txt` (modeled on what `espidf.py` auto-generates) so the
  build is deterministic and the reconfigure triggers are tracked files.
- **platformio.ini:** add `framework = arduino, espidf` to the shared
  core-3 pattern (e.g. a `[common]` interpolation next to
  `platform_core3`) so all seven envs flip together; per-env deviations
  stay in the env sections.
- **sdkconfig strategy:** one committed baseline per chip —
  `sdkconfig.defaults` (shared), plus per-chip/per-env defaults as the
  spike dictates (esp32, esp32s3, esp32p4). Each baseline is diffed against
  Arduino's shipped config with drift documented in the file's header
  comments. Env-specific needs (the TFT TLS block) live with that env.
- **TLS cleanup:** move today's TFT `custom_sdkconfig` block
  (`DTLS=n`, `DYNAMIC_BUFFER=y`, `DYNAMIC_FREE_CONFIG_DATA=y`,
  `KEEP_PEER_CERTIFICATE=n`, `SSL_OUT_CONTENT_LEN=4096`) into the real
  sdkconfig input and **delete all 12 hand `-Wl,--wrap` flags** and their
  explanatory comment blocks. Keep `SSL_IN_CONTENT_LEN` at 16384 (HTTPS OTA
  needs 16k records).
- **Gitignore:** generated `sdkconfig.<env>`, `dependencies.lock`,
  `.dummy/`, and the shim's stamped `sdkconfig.defaults` if it conflicts —
  resolved during implementation so no generated file is ever committed.
- **RAM-knob measurement (not flipping):** record per-env `.bss`/DRAM and
  flash deltas vs the prebuilt baseline; note candidate knobs
  (BT, SPIRAM, WiFi RX buffers, CA bundle) as follow-up work items.

## Validation

Per migrated env: build green; image fits its partition slot; size/RAM diff
recorded. Hardware, using only user-designated units:

- **TFT bench (10.75.1.162):** boot, WiFi, web UI, RAPI (FakeEVSE), mqtts
  to AWS IoT Core (the wrap-flag replacement proof under load), **HTTPS/web
  OTA** — the currently untested path on the TLS build, promoted to a
  required check here.
- **16MB WROOM (10.75.0.28)** and **P4 (10.75.1.143):** flash, boot, WiFi,
  web UI smoke. LVGL display up on P4.
- **Live unit: untouched.**

Regression sentinel: `heap_largest` on the bench build must match the
prebuilt build's profile (±normal variance) over a soak — from-source IDF
must not quietly change heap behavior.

## Non-goals

- Fixing heap fragmentation.
- Dropping the Arduino API (measured and rejected: 63/118 src files use
  Arduino headers, 67 use `String`).
- Flipping any RAM knob (BT, SPIRAM, RX buffers, CA bundle) — measure only.
- Upstream PR — fork branch, bench-proven first. Upstreaming is a later
  decision once TLS + OTA + CI story is clean.
- Touching core-2 envs, partition tables, or application code.

## Risks

| Risk | Mitigation |
|---|---|
| Hybrid mode breaks an LDF lib in a non-obvious way | Spike answers before any real work; ESPAL/ArduinoMongoose are the likely offenders (IDF5 fixes already applied per core-3 migration) |
| sdkconfig drift from Arduino's shipped config changes runtime behavior | Mandatory diff-and-explain in Phase 0.2; heap soak sentinel |
| Build-time regression makes iteration painful | Measured in spike; ninja incremental builds should amortize; accept slower clean builds |
| Old commented-out attempt failed for a reason we can't see | Spike reproduces or refutes cheaply |
| OTA image or bootloader differences from from-source build | HTTPS/web OTA is a required validation step, both directions (onto and off the new build) |
