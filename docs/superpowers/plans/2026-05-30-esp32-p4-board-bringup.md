# ESP32-P4 Board Bring-up (Phase P1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an `openevse_p4` build env (Guition JC4880P443C: ESP32-P4 + ESP32-C6) and get the **core firmware (no on-device display) to compile and link** for the RISC-V P4 target on the pioarduino core-3 platform, with a 16 MB dual-OTA partition layout and WiFi-over-C6 (ESP-Hosted) configured.

**Architecture:** Probe-first, mirroring the P0a approach that worked. Add a minimal P4 env (16 MB, PSRAM, dual-OTA, **no TFT/LVGL display, no NeoPixel/RFID/temp peripherals yet** — those are later phases), build it, and triage the P4/RISC-V-specific fallout. WiFi rides the C6 transparently via `WiFi.h` (the board variant provides the SDIO/ESP-Hosted config). On-device runtime verification (boot, WiFi associate, web UI) is hardware-gated and deferred, exactly like the P0 on-hardware smoke task.

**Tech Stack:** PlatformIO, pioarduino `platform-espressif32` `55.03.38-1` (Arduino-ESP32 3.3.8 / ESP-IDF 5.5), ESP32-P4 (RISC-V) + ESP32-C6 (ESP-Hosted/SDIO), Mongoose web server, MicroOcpp, MicroTasks.

---

## Context from prior phases (read first)

- **P0 is done:** the tree is now **two-core**. `[env]` default platform = `espressif32@6.12.0` (core 2.x, 4 MB boards); 16 MB boards (`openevse_wifi_tft_v1/_dev`) use `${common.platform_core3}` = the pioarduino URL. The P4 env joins the **core-3** side.
- Shared `src/` already compiles on core 3.x (verified on `openevse_wifi_tft_v1`). The LEDC (`LedManagerTask.cpp`) and mbedTLS (`certificates.cpp`) call sites are version-guarded. The `ESPAL`/`ArduinoMongoose` deps are the IDF5-patched RAR forks (pinned by SHA in `[common] lib_deps`).
- **No host test harness** (`test/` is `.http` fixtures only). Verification gate per task = **compile/link** via `~/.platformio/penv/bin/pio run -e openevse_p4`. Real boot/WiFi verification needs the physical board and is a deferred task.
- Confirmed board facts (vendor `esphome-jc4880p443c.yaml` + `pio boards`): module is 16 MB flash / 32 MB PSRAM; C6 on **SDIO slot 1** (CLK18/CMD19/D0=14/D1=15/D2=16/D3=17/RESET=54); pioarduino board `esp32-p4-evboard` is the 16 MB P4+C6 reference whose variant provides the default ESP-Hosted SDIO config; the vendor's Arduino `Wifi_scan` example drove WiFi with plain `WiFi.h` and **no in-code hosted config**, so the variant's defaults are expected to work.

Run all commands from `/home/rar/oevse/openevse_esp32_firmware`. Work on branch `feature/esp32-p4-port`.

---

## Scope

**In scope (P1):** the `openevse_p4` env; 16 MB dual-OTA partitions; getting the core firmware (EVSE logic + web server + TLS + networking-over-C6) to **compile and link** for P4; CI matrix entry.

**Explicitly NOT in P1 (later phases, keep them OFF in this env):**
- On-device display: no `ENABLE_SCREEN_LCD_TFT`, no `gfx_display_libs`, no LVGL. (LVGL+touch is the dedicated display phase.)
- Peripherals: no `NEO_PIXEL_*`, `ENABLE_WS2812FX`, `ENABLE_PN532`, `ENABLE_MCP9808` yet (peripherals phase).
- Runtime/on-hardware verification (boot, WiFi associate, HTTPS, RAPI link): deferred to a hardware task.
- Dual-target (P4+C6) OTA, final RAPI/peripheral pin assignment from the schematic: later phases.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `platformio.ini` | Build envs | Add `[env:openevse_p4]` (core-3, 16 MB, no display/peripherals) |
| `.github/workflows/build.yaml` | CI matrix | Add `openevse_p4` to the env matrix |
| (existing) `openevse_16mb.csv` | 16 MB dual-OTA partitions | Reuse via `${common.build_partitions_16mb}` (no new file) |
| (possibly) `src/*` | P4/RISC-V compile fixes | Only if the probe surfaces them (Task 2), version-guarded |

---

## Task 1: Add the `openevse_p4` env and run the P4 compile probe

Add a minimal P4 env and build it to capture the P4/RISC-V-specific error set. Like the P0a probe, this task may end with a failing build — capturing the errors is its product.

**Files:**
- Modify: `platformio.ini` (add a new `[env:openevse_p4]` near the other core-3 / TFT envs, ~line 519)

- [ ] **Step 1: Add the env**

Append this env to `platformio.ini` (after the `[env:openevse_wifi_tft_v1_dev]` block):

```ini
[env:openevse_p4]
; Guition JC4880P443C: ESP32-P4 + ESP32-C6 (WiFi/BLE via ESP-Hosted/SDIO).
; Core-3 board bring-up: core firmware only — NO display/LVGL, NO peripherals yet.
board = esp32-p4-evboard
platform = ${common.platform_core3}
build_flags =
  ${common.build_flags}
  ${common.src_build_flags}
  ${common.debug_flags}
  -D RAPI_PORT=Serial1
  -D RX1=37
  -D TX1=38
  -D DEBUG_PORT=Serial
lib_deps = ${common.lib_deps}
board_build.partitions = ${common.build_partitions_16mb}
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.flash_mode = qio
```

Notes for the implementer:
- `RX1=37`/`TX1=38` are **placeholder** GPIOs for the RAPI UART so the build is concrete; the real pins come from `5-Schematic/JC4880P443_V1.0.pdf` (one of the board's MX1.25 UART connectors) and are confirmed during the hardware phase. Any valid free P4 GPIO compiles. If 37/38 collide with a variant-reserved pin and the build errors on the pin, pick two other free GPIOs and note it.
- `debug_flags` (not `prod_flags`) is intentional for bring-up — serial debug is wanted on new hardware.
- No `neopixel_lib`/`ws2812fx_lib`/MCP9808 in `lib_deps`, and no display libs — peripherals/display are later phases.

- [ ] **Step 2: Run the probe build and capture errors**

Run: `~/.platformio/penv/bin/pio run -e openevse_p4 2>&1 | tee /tmp/p1-build.log`
(First build downloads the P4 RISC-V toolchain — allow several minutes.)
Expected: may PASS, or FAIL with P4-specific errors. Categorise any failures:
- RISC-V / P4 target compile errors in `src/` (e.g. an xtensa-only assumption, a `WIFI_LED`/pin macro, a missing include).
- ESP-Hosted / `WiFi.h` link errors (the C6 transport not configured for this board — see Task 3).
- Partition/flash config errors.
- Library errors (a dep that doesn't compile for RISC-V).
Note anything that doesn't fit, for Task 2.

- [ ] **Step 3: Commit the env (even if the build fails)**

```bash
git add platformio.ini
git commit -m "feat(p4): add openevse_p4 build env (core-3, 16MB, no display/peripherals)"
```

---

## Task 2: Triage and fix P4/RISC-V compile fallout

Work the errors from `/tmp/p1-build.log`. The fixes are unknowable until the probe runs, so this task is a triage loop with the known candidates spelled out. Keep every fix **version-/target-guarded** so the 2.x and core-3-xtensa (TFT) builds are unaffected.

**Files:**
- Modify: `src/*` as the build dictates (guarded)

- [ ] **Step 1: Re-run and categorise**

Run: `~/.platformio/penv/bin/pio run -e openevse_p4 2>&1 | tee /tmp/p1-build.log`
Run: `grep -nE "error:|undefined reference|fatal error" /tmp/p1-build.log | head -40`

- [ ] **Step 2: Apply fixes by category (guarded)**

- **xtensa-specific code:** if any `src/` file assumes Xtensa (e.g. `#include` of an xtensa header, inline asm, `xt_*`), guard it with `#if CONFIG_IDF_TARGET_ESP32P4` / `#elif` or `#ifdef __riscv`. Provide a P4 path or exclude it.
- **Pin/feature macros undefined for this env:** if a macro the code expects (e.g. `WIFI_LED`, `WIFI_BUTTON`) is referenced unconditionally and now undefined, the existing code already `#ifdef`s most of these — add the missing guard or define a sane value in the env's `build_flags`. (The board's status LED is GPIO26 and boot button GPIO35 per the vendor docs — use those if a button/LED define is needed.)
- **A library that won't compile for RISC-V:** capture which lib + the error. Most (Mongoose, MicroOcpp, ArduinoJson, the forks) are portable C/C++ and should build. If one genuinely doesn't, report it as a blocker needing its own task rather than hacking it.
- **`Serial1`/UART:** on P4 the UART count/pins differ; if `RX1`/`TX1` cause an error, confirm `Serial1` is valid on P4 (it is) and adjust pins.

After each fix, rebuild. Iterate until `src/` compiles.

- [ ] **Step 3: Commit**

```bash
git add src/ platformio.ini
git commit -m "fix(p4): guard target-specific code for ESP32-P4/RISC-V"
```

(If no `src/` changes were needed — i.e. the probe compiled clean — skip this task and note "core firmware compiles for P4 unchanged" in the log.)

---

## Task 3: Confirm WiFi-over-C6 (ESP-Hosted) links

The firmware's networking (`net_manager.cpp`) uses `WiFi.h`; on P4 that is serviced by the C6 over ESP-Hosted, configured by the `esp32-p4-evboard` variant. This task confirms the hosted path **links** (runtime association needs hardware).

**Files:**
- Possibly modify: `platformio.ini` (ESP-Hosted pin overrides, only if needed)

- [ ] **Step 1: Confirm the link includes the hosted/remote-WiFi path**

After Task 2, the full `pio run -e openevse_p4` should link. Confirm there are no unresolved `WiFi`/`esp_wifi_remote`/`esp_hosted` symbols:
Run: `grep -iE "undefined reference|esp_hosted|esp_wifi_remote|WiFiGeneric" /tmp/p1-build.log`
Expected: no undefined references. (Whether the C6 actually associates is a hardware check.)

- [ ] **Step 2: Verify the SDIO pins match the board (documentation check)**

The Guition C6 uses SDIO slot 1: CLK18/CMD19/D0=14/D1=15/D2=16/D3=17/RESET=54 (vendor `esphome-jc4880p443c.yaml`). Compare against the `esp32-p4-evboard` variant defaults:
Run: `grep -riE "hosted|sdio|slot|GPIO18|CLK|d0_pin|reset" ~/.platformio/packages/framework-arduinoespressif32/variants/esp32p4*/ 2>/dev/null | head -30`
- If the variant's hosted pins match (CLK18/CMD19/D0–3=14–17), no change — `WiFi.h` will use them (as the vendor Arduino example did).
- If they differ (esp. RESET=54), record the deltas. Overriding ESP-Hosted pins on Arduino-3.x is done via `sdkconfig`/build-flag (`CONFIG_ESP_HOSTED_*` / the Network `setPins`-equivalent). Capture the exact override needed and apply it as a `build_flag` or a `-D` if the variant exposes it; otherwise note it as a hardware-bring-up item (the pins only matter at runtime, not for the link).

- [ ] **Step 3: Commit (only if a pin override was added)**

```bash
git add platformio.ini
git commit -m "feat(p4): configure C6 ESP-Hosted SDIO pins for JC4880P443C"
```

---

## Task 4: P1 exit — `openevse_p4` compiles + links; add to CI

**Files:**
- Modify: `.github/workflows/build.yaml` (add `openevse_p4` to the matrix)

- [ ] **Step 1: Clean build**

Run: `~/.platformio/penv/bin/pio run -e openevse_p4`
Expected: **[SUCCESS]**, `firmware.bin` under `.pio/build/openevse_p4/`. Record the Flash/RAM figures (16 MB board — should fit with large headroom; the 6.5 MB `openevse_16mb.csv` app slot leaves ample room).

- [ ] **Step 2: Add to the CI matrix**

In `.github/workflows/build.yaml`, add `- openevse_p4` to the `matrix.env` list (after `openevse_wifi_tft_v1_dev`).

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/build.yaml
git commit -m "ci(p4): add openevse_p4 to the build matrix"
```

---

## Deferred to later phases / hardware (NOT this plan)

- **On-hardware bring-up:** flash the Guition board, confirm boot, PSRAM init, C6 association (`WiFi.begin`), web UI over HTTPS, and the RAPI link to the OpenEVSE controller. Needs the physical unit (like the P0 smoke task).
- **Peripherals** (NeoPixel/WS2812, PN532 RFID, MCP9808 temp) on the board's exposed connectors — own phase, with pins from the schematic.
- **LVGL + touch display** (ST7701 MIPI-DSI, GT911) — the dedicated display phase.
- **Dual-target OTA** (P4 app + C6 ESP-Hosted slave firmware) — own phase.
- **Final RAPI/peripheral pin assignment** from `5-Schematic/JC4880P443_V1.0.pdf`.

---

## Final verification (after all tasks)

- [ ] `~/.platformio/penv/bin/pio run -e openevse_p4` → `[SUCCESS]`.
- [ ] The 2.x boards and `openevse_wifi_tft_v1` (core-3) still build (any `src/` guards added in Task 2 didn't regress them): spot-check `pio run -e openevse_wifi_v1` and `pio run -e openevse_wifi_tft_v1`.
- [ ] `openevse_p4` is in the CI matrix.
- [ ] Recorded: P4 Flash/RAM figures; any ESP-Hosted pin deltas vs the variant; any library that needed a P4 fix.

## Notes for the implementer

- **First P4 build is slow** (RISC-V toolchain download).
- **Keep all target-specific `src/` changes guarded** — the same files compile for 2.x (xtensa), core-3 xtensa (TFT), and now core-3 RISC-V (P4). Use `CONFIG_IDF_TARGET_ESP32P4` / `__riscv` / `ESP_ARDUINO_VERSION` / `MBEDTLS_VERSION_NUMBER` as appropriate; never break an existing target.
- This plan delivers a **compiling P4 firmware**, not a running one — runtime is the hardware phase. That mirrors how P0 was validated (compile/link green, boot deferred).
