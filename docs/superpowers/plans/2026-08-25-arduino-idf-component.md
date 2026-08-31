# Arduino as an ESP-IDF Component — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Switch the core-3 envs to pioarduino's hybrid `framework = arduino, espidf` (IDF v5.5.5 from source, Arduino core 3.3.11 as an IDF component), making sdkconfig a real Kconfig-resolved input and INTERFACE `--wrap` link flags propagate automatically.

**Architecture:** Phase 0 spike on `openevse_wifi_tft_v1` gates everything. Then: committed minimal CMakeLists + gitignore, committed sdkconfig baseline with the TFT TLS options, framework flip for the three core-3 envs present on this branch, alternation-thrash check, HW validation on bench units.

**Tech Stack:** PlatformIO + pioarduino platform 55.03.38-1 (`platform_core3`), ESP-IDF 5.5.5, Arduino-ESP32 3.3.11, CMake/ninja + SCons/LDF hybrid.

**Spec:** `docs/superpowers/specs/2026-08-25-arduino-idf-component-design.md`

## Global Constraints

- Commits authored as `Andrew Rankin <andrew@eiknet.com>`; no AI attribution anywhere.
- **Scope on this branch = the three core-3 envs that exist here:** `openevse_wifi_tft_v1`, `openevse_wifi_tft_v1_dev`, `openevse_wifi_v1_16mb`. The s3_lcd*/p4/16mb_fake envs live on other branches and adopt the shared pattern when they rebase — do NOT invent them here.
- Core-2 envs, `native*` envs, partition tables, and application source (`src/*.cpp|h`) are untouched. `platformio.ini`, new `CMakeLists.txt`, `sdkconfig*` inputs, `.gitignore` are the only editable files (plus docs).
- **Verify the artifact, not the config:** every sdkconfig claim is proven against `firmware.elf` (`nm`) or `firmware.map`, never against `sdkconfig.<env>` alone. Toolchain nm: `find ~/.platformio/packages -name "*-nm" -path "*xtensa*"` and use the esp32 one.
- No generated files committed: `sdkconfig.openevse_*` (resolved outputs), `dependencies.lock`, `managed_components/`, `.dummy/`, build dirs.
- Hardware: bench TFT = `10.75.1.162` (OTA: `curl -F firmware=@$BIN http://10.75.1.162/update`), 16MB WROOM = `10.75.0.28`. **The live TFT unit is never flashed, rebooted, or POSTed.** Flashing the bench replaces its current AWS-mqtts build — its NVS config (incl. AWS broker settings) persists across OTA, so mqtts revalidates on the new image.
- `SSL_IN_CONTENT_LEN` stays 16384 (HTTPS OTA needs 16k TLS records).
- Build commands run from the worktree root; `pio run -e <env>` with `2>&1 | tee` into the plan workspace, and completion detected with an `until`-loop on the log terminator (`^=+ \[` or explicit exit marker) — no bare `sleep` chains, no grepping for `SUCCESS|FAILED` (false-matches BT source names).

---

### Task 1: Prebuilt baselines (measurement only, no commit)

**Files:** none modified. Output: `<workspace>/baseline-prebuilt.md`.

**Interfaces:** Produces the baseline table Tasks 2 and 7 diff against: per env — clean build wall time, `firmware.bin` size, and `size` section totals (text/data/bss).

- [ ] **Step 1: Build all three envs prebuilt (current state of this branch)**

```bash
for e in openevse_wifi_tft_v1 openevse_wifi_tft_v1_dev openevse_wifi_v1_16mb; do
  /usr/bin/time -f "%e s" pio run -e $e 2>&1 | tail -20
done
```
Expected: three SUCCESS builds (this branch built green before; if a build fails here, STOP — pre-existing breakage, report it).

- [ ] **Step 2: Record sizes**

```bash
NM_DIR=$(dirname $(find ~/.platformio/packages -name "xtensa-esp32-elf-size" | head -1))
for e in openevse_wifi_tft_v1 openevse_wifi_tft_v1_dev openevse_wifi_v1_16mb; do
  echo "== $e"; ls -l .pio/build/$e/firmware.bin; $NM_DIR/xtensa-esp32-elf-size .pio/build/$e/firmware.elf
done
```
Write the table (env, wall time, bin bytes, text/data/bss) to `<workspace>/baseline-prebuilt.md`. Also record `nm` output count for `__wrap_mbedtls_ssl_` (expected 0 references — nothing is wrapped today).

### Task 2: Phase 0 spike — hybrid build of openevse_wifi_tft_v1 (GO/NO-GO GATE)

**Files:**
- Modify (throwaway, revert at end): `platformio.ini` (TFT env only)
- Output: `<workspace>/spike-report.md`

**Interfaces:** Produces the go/no-go verdict + the facts Tasks 3–4 are built from: whether CMakeLists were auto-generated and their content; where the seed sdkconfig came from; how `components/esp_tsdb` resolved; whether `custom_sdkconfig` works in hybrid mode; build cost.

- [ ] **Step 1: Flip the framework on the TFT env only**

In `platformio.ini` `[env:openevse_wifi_tft_v1]`, add one line under `platform = ${common.platform_core3}`:

```ini
framework = arduino, espidf
```

- [ ] **Step 2: First hybrid build; watch the esp_tsdb double-resolution hazard**

```bash
pio run -e openevse_wifi_tft_v1 2>&1 | tee <workspace>/spike-build1.log
```
Expected: either SUCCESS or an instructive failure. Record in the report:
- Were `CMakeLists.txt` and `src/CMakeLists.txt` auto-generated? Copy their content into the report (they become Task 3's committed versions).
- **esp_tsdb hazard:** `components/esp_tsdb` is BOTH a `file://` lib_dep AND sits in `components/` — IDF's default extra-components dir. Check the build log and `firmware.map` for duplicate tsdb objects or multiple-definition link errors. If it double-builds, the fix (choose one, record which): remove it from `lib_deps` and let it build as a pure IDF component, or move/rename the dir out of `components/`. The chosen fix must keep `ENABLE_TSDB` compiling and carries into Task 4.
- Did all other lib_deps (ArduinoJson, ArduinoMongoose, ESPAL, MicroOcpp×2, LVGL, display/NeoPixel/WS2812FX libs, MCP9808) compile via LDF and link? List any that failed and the failure mode.

- [ ] **Step 3: sdkconfig parity diff**

```bash
diff <(sort sdkconfig.openevse_wifi_tft_v1) \
     <(sort ~/.platformio/packages/framework-arduinoespressif32-libs/esp32/sdkconfig) | head -100
```
(Adjust the prebuilt-sdkconfig path to wherever the libs package actually keeps it.) Every delta goes in the report with a one-line explanation or a `NEEDS-EXPLANATION` marker. Unexplained behavior-relevant deltas (WiFi buffers, lwIP, FreeRTOS tick/stack, mbedTLS lengths) are a no-go signal.

- [ ] **Step 4: The --wrap proof with zero hand flags**

Add to the TFT env (still throwaway):

```ini
custom_sdkconfig =
  CONFIG_MBEDTLS_SSL_PROTO_DTLS=n
  CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
  CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
  CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=n
  CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
```
Rebuild, then verify in the artifact:

```bash
$NM_DIR/xtensa-esp32-elf-nm .pio/build/openevse_wifi_tft_v1/firmware.elf | grep -c __wrap_mbedtls_ssl_
grep -B2 "__wrap_mbedtls_ssl_read" .pio/build/openevse_wifi_tft_v1/firmware.map | head
grep "CONFIG_MBEDTLS_DYNAMIC_BUFFER" sdkconfig.openevse_wifi_tft_v1
```
Expected: wrap symbols present AND referenced (map shows callers resolving to `__wrap_*`), `DYNAMIC_BUFFER=y` in the resolved sdkconfig, with **no** `-Wl,--wrap` anywhere in `platformio.ini`. If `custom_sdkconfig` is ignored in hybrid mode, put the same lines in a committed `sdkconfig.defaults`-style input instead and record that as the Task 4 mechanism.

- [ ] **Step 5: Cost + alternation probe**

Record: clean-build wall time, incremental (touch `src/app_config.cpp`) rebuild time, `firmware.bin` delta vs Task 1 baseline, and fit in the 0x640000 app slot. Then `pio run -e openevse_wifi_v1_16mb` (still prebuilt) followed by `pio run -e openevse_wifi_tft_v1` again — confirm NO framework wipe/re-download (the `check_reinstall_frwrk()` thrash this migration kills) and note whether the hybrid rebuild was incremental.

- [ ] **Step 6: Bench boot smoke**

Flash bench via OTA: `curl -F firmware=@.pio/build/openevse_wifi_tft_v1/firmware.bin http://10.75.1.162/update`. Verify: device returns, `curl http://10.75.1.162/status` shows sane heap fields, web UI loads, LCD renders. NEVER the live unit.

- [ ] **Step 7: Verdict + revert**

Write GO or NO-GO with reasons to `<workspace>/spike-report.md`. Revert `platformio.ini` (`git checkout platformio.ini`). NO-GO ⇒ stop the plan, deliver the report. GO ⇒ continue.

### Task 3: Committed build scaffolding — CMakeLists + gitignore

**Files:**
- Create: `CMakeLists.txt`, `src/CMakeLists.txt`
- Modify: `.gitignore`

**Interfaces:** Produces the tracked CMake files whose content came from the spike's auto-generated versions; Task 4 builds on them.

- [ ] **Step 1: Write the two CMakeLists** with the spike-captured content. Expected shape (adjust to what the spike actually generated — the generated content wins over this sketch):

```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.16)
list(APPEND EXTRA_COMPONENT_DIRS src)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openevse_esp32_firmware)
```
```cmake
# src/CMakeLists.txt
idf_component_register(SRCS ${app_sources})
```
Add a short header comment in each: used only by the `framework = arduino, espidf` envs; core-2 envs ignore these files.

- [ ] **Step 2: Extend `.gitignore`**

```
sdkconfig.openevse_*
dependencies.lock
managed_components/
.dummy/
```
(Do NOT ignore the committed sdkconfig *inputs* Task 4 adds — name them explicitly if a pattern would swallow them.)

- [ ] **Step 3: Prove core-2 unaffected**

```bash
pio run -e openevse_wifi_v1 2>&1 | tail -5
```
Expected: SUCCESS, byte-identical behavior (CMakeLists are inert for framework=arduino envs).

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt .gitignore
git commit -m "build: add CMake scaffolding for arduino+espidf hybrid envs"
```

### Task 4: Flip the TFT envs with the TLS sdkconfig

**Files:**
- Modify: `platformio.ini` (`[common]` + `[env:openevse_wifi_tft_v1]`; `_dev` inherits via `extends`)

**Interfaces:** Produces the shared flip pattern (`framework = arduino, espidf` next to `platform_core3` usage) and the TLS options block, via whichever mechanism the spike proved (`custom_sdkconfig` or committed defaults file). Task 5 reuses the pattern verbatim.

- [ ] **Step 1: Apply the spike's proven config as a permanent change** — framework line + TLS options block + the esp_tsdb resolution fix the spike chose, with a comment block stating: options are real Kconfig now; misspelled/unsatisfiable options fail loudly; the 12 hand `--wrap` flags this replaces (see spec).

- [ ] **Step 2: Build both TFT envs and re-run the artifact proof**

```bash
pio run -e openevse_wifi_tft_v1 && pio run -e openevse_wifi_tft_v1_dev
$NM_DIR/xtensa-esp32-elf-nm .pio/build/openevse_wifi_tft_v1/firmware.elf | grep -c __wrap_mbedtls_ssl_
```
Expected: both SUCCESS; wrap count > 0 and referenced in the map; bin fits 0x640000.

- [ ] **Step 3: Negative test (Kconfig now fails loudly)** — temporarily add `CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT=y` (does not exist in IDF 5.5.5); expected: the build reports/rejects it rather than silently accepting. Remove it. Record actual behavior in the workspace notes.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini   # plus any committed sdkconfig input file
git commit -m "build(tft): switch openevse_wifi_tft_v1 to arduino+espidf; TLS dynamic-buffer via real sdkconfig"
```

### Task 5: Flip openevse_wifi_v1_16mb + alternation check

**Files:**
- Modify: `platformio.ini` (`[env:openevse_wifi_v1_16mb]`)

- [ ] **Step 1: Apply the same framework line** (no TLS block — this env keeps stock mbedTLS; that asymmetry is deliberate and gets a one-line comment).

- [ ] **Step 2: Build + alternation-thrash check**

```bash
pio run -e openevse_wifi_v1_16mb && pio run -e openevse_wifi_tft_v1 && pio run -e openevse_wifi_v1_16mb
```
Expected: all SUCCESS; no framework package wipe/re-download between envs; second and third runs mostly incremental. Record times.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "build(16mb): switch openevse_wifi_v1_16mb to arduino+espidf"
```

### Task 6: Hardware validation

**Files:** none. Output: `<workspace>/hw-validation.md`.

- [ ] **Step 1: Bench TFT (10.75.1.162)** — OTA the Task 4 `openevse_wifi_tft_v1` build. Verify: boots, WiFi associates, web UI, LCD renders, `/status` heap fields sane. Then **HTTPS/web OTA off the new build** (OTA the same image again through the new firmware's own /update) — this is the previously-untested path and is REQUIRED. Then mqtts: bench NVS still holds the AWS IoT config; enable MQTT and confirm TLS handshake completes and stays connected ≥10 min with `heap_largest` recorded before/after.
- [ ] **Step 2: 16MB WROOM (10.75.0.28)** — OTA the Task 5 build. Verify: boots, WiFi, web UI, `/status` sane.
- [ ] **Step 3: Heap sentinel soak** — start a background 60 s-interval `/status` logger against the bench TFT for ≥2 h (`until`-loop, background task); `heap_largest` profile must match the prebuilt baseline's shape (±normal variance). Divergence = report, do not ship.
- [ ] **Step 4: Live unit** — nothing. Confirm in the report that it was not touched.

### Task 7: Measurement report + docs

**Files:**
- Create: `docs/arduino-idf-component-notes.md` (or fold into the spec — implementer's choice, one place only)

- [ ] **Step 1: Final diff table** — per env: bin size, text/data/bss, clean/incremental build time, prebuilt vs hybrid (Task 1 vs Tasks 4–5 numbers).
- [ ] **Step 2: RAM-knob candidates recorded, NOT flipped** — from the hybrid map/nm: BT (`CONFIG_BT_ENABLED`) DRAM .bss total, `CONFIG_SPIRAM`, `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM`, CA bundle. One table row each: knob, measured cost, risk, "follow-up".
- [ ] **Step 3: Commit**

```bash
git add docs/
git commit -m "docs: arduino+espidf migration measurements and follow-up RAM knobs"
```
