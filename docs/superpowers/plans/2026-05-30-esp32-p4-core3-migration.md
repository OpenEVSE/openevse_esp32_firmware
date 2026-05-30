# Arduino-ESP32 Core 3.x Migration (Phase P0) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the entire OpenEVSE firmware build from Arduino-ESP32 2.0.x (ESP-IDF 4.4, `espressif32@6.12.0`) to Arduino-ESP32 3.x (ESP-IDF 5.3, pioarduino platform), so every existing board still builds and boots — the prerequisite for adding the ESP32-P4 target.

**Architecture:** Two stages. **P0a (probe):** repoint the single most representative env (`openevse_wifi_v1`, which exercises the full web-server + Mongoose + mbedTLS stack) at the pioarduino core-3.x platform and drive it to a clean compile/link, fixing the known breakers (LEDC API, mbedTLS opaque structs, WiFi events) as they surface. This de-risks the whole effort. **P0b (roll-out):** apply the same platform to every remaining env and get the full build matrix green, then smoke-boot on hardware.

**Tech Stack:** PlatformIO, Arduino-ESP32 3.x via the **pioarduino** `platform-espressif32` fork, ESP-IDF 5.3, mbedTLS 3.x, ArduinoMongoose (Mongoose 6.14), MicroOcpp, MicroTasks.

---

## Testing approach for this plan (read first)

This is **embedded firmware with no host unit-test harness**: `test/` holds only `.http` API fixtures; there is no `native` PlatformIO env, no Unity/gtest. (The one native artifact, `divert_sim/`, is a standalone charge-divert simulator unrelated to the firmware build.) Classic red-green unit TDD does not apply here and will not be faked — this matches the convention already established by the sibling plan in this repo (`docs/specs/2026-05-26-lvgl-tft-spike-plan.md`).

The verification gate for **every** task is a **compile/link gate**, plus an on-hardware smoke boot at the end:

- **Probe / primary env:** `~/.platformio/penv/bin/pio run -e openevse_wifi_v1`
- **Full matrix (P0b):** every env listed in `.github/workflows/build.yaml`.

"PASS" for a task means the listed `pio run` exits `0` (look for `========= [SUCCESS] =========`). The **first** build after changing the platform downloads the entire core-3.x toolchain + IDF 5.3 and takes many minutes; subsequent builds are cached.

Run all commands from the repo root: `/home/rar/oevse/openevse_esp32_firmware`.

Work happens on branch `feature/esp32-p4-port` (already created; the design spec is committed there). If using subagent-driven execution in an isolated worktree, branch from `feature/esp32-p4-port`.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `platformio.ini` | Platform pin + global build flags | Modify `[env]` platform; add `MBEDTLS_ALLOW_PRIVATE_ACCESS` to `[common] build_flags` |
| `src/LedManagerTask.cpp` | RGB-LED LEDC PWM | Port `ledcSetup`/`ledcAttachPin`/`ledcWrite` to the 3.x unified API |
| `src/certificates.cpp` | Cert parsing via mbedTLS | Resolve opaque-struct access (covered by the build flag; accessor fallback documented) |
| `src/net_manager.cpp` | WiFi event handling, `esp_wifi_set_country_code` | Verify under 3.x; fix only if the build flags it |
| `.github/workflows/build.yaml` | CI build matrix | No change for P0 (platform flows from `platformio.ini`); confirmation step only |

`lib/` is empty and `.gitmodules` only references the `gui-v2` web UI (JS, not compiled into firmware), so there are no vendored C deps to patch — all libraries come from the PlatformIO registry via `platformio.ini`.

---

## Task 1: Pin the pioarduino core-3.x platform and run the probe build

Repoint the build at the pioarduino platform and immediately build the primary env to capture the real error set that drives Tasks 2–5. This task is expected to **fail to compile** — that failure is its product.

**Files:**
- Modify: `platformio.ini` (the `[env]` section, line ~180: `platform = espressif32@6.12.0`)

- [ ] **Step 1: Resolve and pin the exact pioarduino release**

Open <https://github.com/pioarduino/platform-espressif32/releases>. Pick the latest **stable** release whose notes bundle **Arduino-ESP32 3.x** (3.2.x or newer). Note its tag (e.g. `53.03.xx`). Do **not** guess a tag — the release list is authoritative and changes over time. Record the chosen tag for use below; this plan refers to it as `<PIOARDUINO_TAG>`.

(Rationale: P0 only migrates the existing ESP32 boards, so any 3.x works. The later P4 phases may need to pin specifically Arduino-ESP32 **3.2.1** to match the board vendor's ST7701/GT911 drivers — that constraint is recorded in the design spec, not enforced here.)

- [ ] **Step 2: Repoint the platform**

In `platformio.ini`, in the `[env]` section, replace:

```ini
platform = espressif32@6.12.0
```

with (substituting the tag resolved in Step 1):

```ini
# pioarduino fork — Arduino-ESP32 3.x / ESP-IDF 5.3. Required for ESP32-P4 and
# the core-3.x migration. See docs/superpowers/specs/2026-05-30-esp32-p4-port-design.md
platform = https://github.com/pioarduino/platform-espressif32/releases/download/<PIOARDUINO_TAG>/platform-espressif32.zip
```

- [ ] **Step 3: Run the probe build and capture the errors**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 2>&1 | tee /tmp/p0a-build.log`
Expected: **FAIL** (first run downloads the toolchain, then errors). Skim `/tmp/p0a-build.log` and confirm the failures fall into the anticipated buckets:
- `ledc*` errors in `LedManagerTask.cpp` → Task 2.
- `mbedtls` / opaque-field errors in `certificates.cpp` and/or the Mongoose TLS glue → Tasks 3 & 5.
- any `WiFiEvent`/`esp_wifi_set_country_code` errors in `net_manager.cpp` → Task 4.

If a failure does **not** fit these buckets, note it — it becomes an extra task before Task 6.

- [ ] **Step 4: Commit the platform pin**

```bash
git add platformio.ini
git commit -m "build: migrate platform to pioarduino Arduino-ESP32 3.x (core-3 migration P0)"
```

---

## Task 2: Port the LEDC (RGB-LED PWM) API to 3.x

In core 3.x the channel-based LEDC API is gone: `ledcSetup(ch, freq, res)` + `ledcAttachPin(pin, ch)` collapse into `ledcAttach(pin, freq, res)`, and `ledcWrite` takes the **pin**, not the channel. `LedManagerTask.cpp` uses the old API in three places. The `*_LEDC_CHANNEL` macros become unused for attach/write (kept only as harmless defines).

**Files:**
- Modify: `src/LedManagerTask.cpp` (lines ~208–213, ~595–597, ~723–725)

- [ ] **Step 1: Port the RGB attach block**

In `src/LedManagerTask.cpp`, replace lines 208–213:

```cpp
  ledcSetup(RED_LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(RED_LED, RED_LEDC_CHANNEL);
  ledcSetup(GREEN_LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(GREEN_LED, GREEN_LEDC_CHANNEL);
  ledcSetup(BLUE_LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(BLUE_LED, BLUE_LEDC_CHANNEL);
```

with:

```cpp
  // core 3.x unified LEDC API: attach pin directly with freq + resolution.
  ledcAttach(RED_LED, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttach(GREEN_LED, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttach(BLUE_LED, LEDC_FREQUENCY, LEDC_RESOLUTION);
```

- [ ] **Step 2: Port the RGB write block**

Replace lines 595–597:

```cpp
  ledcWrite(RED_LEDC_CHANNEL, gamma8[wifiRed]);
  ledcWrite(GREEN_LEDC_CHANNEL, gamma8[wifiGreen]);
  ledcWrite(BLUE_LEDC_CHANNEL, gamma8[wifiBlue]);
```

with (3.x `ledcWrite` is keyed by pin):

```cpp
  ledcWrite(RED_LED, gamma8[wifiRed]);
  ledcWrite(GREEN_LED, gamma8[wifiGreen]);
  ledcWrite(BLUE_LED, gamma8[wifiBlue]);
```

- [ ] **Step 3: Port the shared-button-LED block**

Replace lines 723–725:

```cpp
  ledcAttachPin(WIFI_BUTTON_SHARE_LED, WIFI_BUTTON_SHARE_LEDC_CHANNEL);
  ledcWrite(WIFI_BUTTON_SHARE_LED, buttonShareState);
```

with:

```cpp
  ledcAttach(WIFI_BUTTON_SHARE_LED, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcWrite(WIFI_BUTTON_SHARE_LED, buttonShareState);
```

(Note: line ~102 has a pre-existing malformed macro `WIFI_BUTTON_SHARE_LLEDC_CHANNELBLUE_LEDC_CHANNEL` in the `BLUE_LED == WIFI_BUTTON_SHARE_LED` branch. It is only reached if `WIFI_LED == BLUE_LED` on a board. Leave it unless a build in Task 7 actually trips it; if so, fix to `#define WIFI_BUTTON_SHARE_LEDC_CHANNEL BLUE_LEDC_CHANNEL`. Out of scope otherwise.)

- [ ] **Step 4: Rebuild and confirm the LEDC errors are gone**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 2>&1 | tee /tmp/p0a-build.log`
Expected: still FAIL overall, but **no `ledc*` errors** remain in the log (`grep -i ledc /tmp/p0a-build.log` shows none). Remaining errors are the mbedTLS / Mongoose ones.

- [ ] **Step 5: Commit**

```bash
git add src/LedManagerTask.cpp
git commit -m "fix(led): port LEDC PWM calls to Arduino-ESP32 3.x unified API"
```

---

## Task 3: Allow mbedTLS 3.x private-struct access

IDF 5 ships **mbedTLS 3.x**, which marks many struct fields private. `certificates.cpp` reads `x509.issuer`, `x509.subject`, `x509.serial.len`, and `x509.serial.p[i]` directly, and ArduinoMongoose's TLS glue does similar. The lowest-risk, codebase-wide remedy is the documented mbedTLS escape hatch: define `MBEDTLS_ALLOW_PRIVATE_ACCESS`, which re-exposes the fields without source changes. Add it once, globally.

**Files:**
- Modify: `platformio.ini` (`[common] build_flags`, the block starting line ~80)

- [ ] **Step 1: Add the build flag**

In `platformio.ini`, in `[common]` `build_flags =`, add this line near the other global `-D` defines (e.g. right after `-D ESP32`):

```ini
  -D MBEDTLS_ALLOW_PRIVATE_ACCESS
```

- [ ] **Step 2: Rebuild and confirm certificate-struct errors clear**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 2>&1 | tee /tmp/p0a-build.log`
Expected: no `x509`/`serial`/opaque-field errors from `certificates.cpp` remain (`grep -iE "certificates\.cpp|x509|MBEDTLS_PRIVATE" /tmp/p0a-build.log` shows no errors). Mongoose TLS errors (if any) are handled in Task 5.

- [ ] **Step 3 (fallback, only if the flag does not clear `certificates.cpp`):** Refactor the direct field reads to mbedTLS API. Replace the serial loop at lines 44–45:

```cpp
    for(int i = 0; i < x509.serial.len; i++) {
      id = id << 8 | x509.serial.p[i];
    }
```

with access via the (still-public-with-flag) buffer, or compute the id from `mbedtls_x509_crt`'s serial using `MBEDTLS_PRIVATE(serial)`:

```cpp
    const mbedtls_x509_buf &serial = x509.MBEDTLS_PRIVATE(serial);
    for(size_t i = 0; i < serial.MBEDTLS_PRIVATE(len); i++) {
      id = id << 8 | serial.MBEDTLS_PRIVATE(p)[i];
    }
```

and similarly wrap `&x509.issuer` / `&x509.subject` as `&x509.MBEDTLS_PRIVATE(issuer)` / `&x509.MBEDTLS_PRIVATE(subject)` in the `ENABLE_DEBUG_CETRIFICATES` block. (Only needed if Step 1's flag is insufficient — normally it is sufficient.)

- [ ] **Step 4: Commit**

```bash
git add platformio.ini src/certificates.cpp
git commit -m "fix(tls): allow mbedTLS 3.x private struct access for cert parsing"
```

---

## Task 4: Verify WiFi event handling and esp_wifi_set_country_code under 3.x

`net_manager.cpp` uses `WiFi.onEvent`, `WiFiEvent_t`, `arduino_event_info_t`, `ARDUINO_EVENT_*` constants, and one direct `esp_wifi_set_country_code("01", true)`. These symbols still exist in core 3.x (the `ARDUINO_EVENT_*` enum is the 3.x-native form), so this is expected to compile unchanged — but it must be confirmed, since event/info struct layouts are load-bearing for connectivity. This is a verify-then-fix-if-needed task.

**Files:**
- Inspect / conditionally modify: `src/net_manager.cpp` (event handler ~303–501, `esp_wifi_set_country_code` line 98)

- [ ] **Step 1: Rebuild and check for net_manager errors**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 2>&1 | tee /tmp/p0a-build.log`
Run: `grep -nE "net_manager|WiFiEvent|arduino_event|country_code|onEvent" /tmp/p0a-build.log`
Expected: **no errors** referencing these. If clean, no code change — proceed to Step 3.

- [ ] **Step 2 (only if errors appear):** Apply the targeted fix matching the error:
  - If `esp_wifi_set_country_code` signature changed: it remains `esp_err_t esp_wifi_set_country_code(const char *country, bool ieee80211d_enabled)` in IDF 5.3 — confirm the include `<esp_wifi.h>` resolves; no call change expected.
  - If an `ARDUINO_EVENT_*` constant was renamed/removed: map it to its 3.x equivalent (the enum is defined in `WiFiGeneric.h`; search there for the closest `ARDUINO_EVENT_WIFI_*` / `ARDUINO_EVENT_ETH_*` name) and update the `switch` case.
  - If `arduino_event_info_t` field names drifted (e.g. `info.got_ip.ip_info`): update to the 3.x field path shown in `WiFiGeneric.h`.

- [ ] **Step 3: Commit (only if Step 2 changed code)**

```bash
git add src/net_manager.cpp
git commit -m "fix(net): adapt WiFi event handling to Arduino-ESP32 3.x"
```

If no change was needed, skip the commit and note "net_manager compiles unchanged on 3.x" in the task log.

---

## Task 5: Resolve the ArduinoMongoose / MicroOcpp TLS compile fallout

This is the empirical core of the migration and the reason for the probe. `ArduinoMongoose` (Mongoose 6.14, `MG_SSL_IF_MBEDTLS`) and `MicroOcppMongoose` are registry deps compiled against mbedTLS 3.x for the first time. Work the remaining errors in `/tmp/p0a-build.log` after Tasks 2–4 using the decision tree below; each branch is a concrete, bounded action.

**Files:**
- Possibly modify: `platformio.ini` (`[common] lib_deps`, the Mongoose flags ~83–110)
- Possibly create: a patched fork referenced via `lib_deps` (only if upstream cannot build)

- [ ] **Step 1: Categorise the remaining errors**

Run: `grep -iE "mongoose|mbedtls|ssl|/ArduinoMongoose/|MicroOcpp" /tmp/p0a-build.log | grep -iE "error|undefined"`
Classify each into one of the branches in Step 2.

- [ ] **Step 2: Apply the matching remedy (in increasing order of effort)**

  - **(a) Opaque-struct field access inside Mongoose's mbedTLS glue** → already addressed by `MBEDTLS_ALLOW_PRIVATE_ACCESS` (Task 3). If such errors persist, confirm the flag reaches the library build (it is a global `build_flags`, so it should). No further action.
  - **(b) Removed/renamed mbedTLS *functions*** (e.g. `mbedtls_ssl_conf_max_frag_len`, legacy RNG, `mbedtls_*_ret` suffixes dropped in 3.x): these live in the library, not `src/`. Prefer **bumping the dependency**: check the PlatformIO registry / GitHub for a newer `jeremypoulter/ArduinoMongoose` (or `MicroOcppMongoose`) release tagged for IDF 5 / mbedTLS 3, and update the version in `[common] lib_deps`. Rebuild.
  - **(c) No compatible upstream release exists** → fork the offending library, patch the handful of mbedTLS-3 call sites (replace `*_ret` names, drop removed config calls, use the 3.x RNG signature), and reference the fork from `lib_deps` as a Git URL pinned to a commit. Keep the patch minimal and documented in the fork's commit message.
  - **(d) A `-D MG_SSL_*` build flag is now invalid** → reconcile the Mongoose SSL flags (`platformio.ini` ~83–110) with what the (possibly bumped) library expects; remove flags the new version rejects.

- [ ] **Step 3: Rebuild after each remedy until the TLS errors clear**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 2>&1 | tee /tmp/p0a-build.log`
Iterate Step 2 until `grep -iE "error" /tmp/p0a-build.log` is empty for the mongoose/mbedtls bucket.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini
git commit -m "build(tls): make ArduinoMongoose/MicroOcpp build against mbedTLS 3.x (IDF5)"
```

(If a library fork was required, the commit also records the new `lib_deps` Git URL + pinned commit.)

---

## Task 6: P0a exit — clean compile + link of the primary env

The probe succeeds when `openevse_wifi_v1` (full web + TLS + OCPP + RFID + LED + LittleFS stack) builds and links on core 3.x. This is the go/no-go gate for the whole migration.

**Files:** none (verification + tag)

- [ ] **Step 1: Full clean build**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1`
Expected: **PASS** — `========= [SUCCESS] =========`, a `firmware.bin` produced under `.pio/build/openevse_wifi_v1/`.

- [ ] **Step 2: Confirm the binary fits the partition**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 2>&1 | grep -iE "Flash:|RAM:"`
Expected: Flash usage under the app partition size (`min_spiffs.csv` app slot). If it overflows (IDF 5 images are larger), note it for the partition-sizing work in the design's P5 — do not block P0a, but record the headroom.

- [ ] **Step 3: Tag the probe result**

```bash
git tag p0a-probe-green
```

Record in the task log: final flash/RAM figures, and whether any library fork was needed (this informs the P4 effort).

---

## Task 7: P0b — roll the platform across every remaining env

With the shared `src/` proven on core 3.x via the primary env, build every other env from the CI matrix. Because the platform and all fixes live in shared `[common]`/`[env]` config and shared source, most envs should build with no further change; per-board fallout (if any) is narrow.

**Files:**
- Possibly modify: `platformio.ini` (per-env blocks, only if a specific board errors)

- [ ] **Step 1: Build the full matrix**

Run each env from `.github/workflows/build.yaml`. A loop:

```bash
for e in nodemcu-32s espressif_esp-wrover-kit espressif_esp-wrover-kit_latest \
         adafruit_huzzah32_dev adafruit_huzzah32 adafruit_featheresp32 \
         olimex_esp32-gateway-old olimex_esp32-gateway-e olimex_esp32-gateway-f \
         olimex_esp32-gateway-e_dev olimex_esp32-gateway-f_dev olimex_esp32-poe-iso \
         heltec_esp32-wifi-lora-v2 wt32-eth01 esp32-c3-devkitc-02 \
         elecrow_esp32_hmi openevse_wifi_tft_v1 openevse_wifi_tft_v1_dev; do
  echo "=== $e ==="; ~/.platformio/penv/bin/pio run -e $e 2>&1 | tail -3
done
```

Expected: every env prints `[SUCCESS]`. (`openevse_wifi_v1` is already green from Task 6.)

- [ ] **Step 2: Fix any per-board failures**

For each failing env, triage from its log. Likely causes and concrete fixes:
- **`espressif_esp-wrover-kit_latest`** uses bare `platform = espressif32` — repoint it to the same pioarduino URL as `[env]` (or have it `extends` an env that does), so it does not pull the old Espressif platform.
- **Wired-Ethernet boards** (`olimex_*`, `wt32-eth01`): the `ETH` API changed in 3.x (the `ETH.begin(...)` signature is now config-struct based). If `ETH`/`ETH_PHY_*` errors appear, port the `ENABLE_WIRED_ETHERNET` path in `net_manager.cpp` to the 3.x `ETH.begin()` overload. Keep this change gated to the wired path so non-Ethernet boards are unaffected.
- **`esp32-c3-devkitc-02`**: RISC-V target — confirm the pioarduino platform provides the C3 toolchain (it does); no code change expected.

Commit each board fix separately, e.g.:

```bash
git add src/net_manager.cpp platformio.ini
git commit -m "fix(eth): port wired-Ethernet init to Arduino-ESP32 3.x ETH API"
```

- [ ] **Step 3: Confirm CI picks up the platform with no workflow edit**

`.github/workflows/build.yaml` installs PlatformIO via pip and reads `platform` from `platformio.ini`, so the migration flows automatically; the env matrix names are unchanged. No edit required. (The P4 env gets added to this matrix in a later plan.) Verify by reading the workflow and confirming it has no hard-coded `espressif32@` reference.

- [ ] **Step 4: Commit any remaining config**

```bash
git add platformio.ini
git commit -m "build: complete core-3.x platform roll-out across all envs (P0b)"
```

---

## Task 8: On-hardware smoke boot (the real gate)

Compilation proves the migration links; only hardware proves it runs. This requires a physical OpenEVSE WiFi unit (`openevse_wifi_v1`). Hand off to whoever holds the board if the implementer cannot flash.

**Files:** none (manual verification)

- [ ] **Step 1: Flash the primary env**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1 -t upload` (USB), or OTA per the repo convention:
`curl -F firmware=@.pio/build/openevse_wifi_v1/firmware.bin http://<device>/update`

- [ ] **Step 2: Verify boot + core functions over serial/web**

Confirm, via the serial monitor (`~/.platformio/penv/bin/pio device monitor -e openevse_wifi_v1`) and the web UI:
- Boots without a crash loop; `IDF version` line shows **5.3.x**.
- LittleFS mounts (no "LittleFS Mount Failed").
- WiFi connects (STA) and the web UI loads over **HTTPS** (exercises the migrated mbedTLS/Mongoose path).
- RAPI link to the OpenEVSE controller is alive (charge state shows on the web UI).
- RGB/WS2812 LED behaves (exercises the LEDC fix).
- An OTA update via `/update` succeeds and reboots cleanly.

- [ ] **Step 3: Record results**

Note any runtime regressions in the task log. Runtime fixes (vs compile fixes) are tracked as follow-ups; a clean boot + WiFi + HTTPS + RAPI + OTA is the P0 done-criterion.

---

## Final verification (after all tasks)

- [ ] `openevse_wifi_v1` builds: `~/.platformio/penv/bin/pio run -e openevse_wifi_v1` → `[SUCCESS]`
- [ ] Full matrix builds: every env in `build.yaml` → `[SUCCESS]` (Task 7 loop all green)
- [ ] `git log --oneline` shows per-task commits on `feature/esp32-p4-port`; tag `p0a-probe-green` exists.
- [ ] Hardware smoke boot passed (Task 8) or is explicitly handed off to the board holder.
- [ ] Recorded: final flash/RAM headroom, and whether any dependency had to be forked/bumped (feeds the P1–P5 planning).

## Notes for the implementer

- **First build is slow and network-bound** (downloads the entire pioarduino toolchain + IDF 5.3). Allow 10+ minutes; later builds are cached under `~/.platformio`.
- **Work the probe (Tasks 1–6) to completion before touching other boards.** If Task 5 reveals that a core dependency cannot build on IDF 5 even after a fork attempt, **stop and re-plan** — that is the migration-killer risk the probe exists to surface early.
- **Keep fixes gated and minimal.** The shared `src/` must keep building for all boards; prefer global build flags and 3.x-native API swaps over `#ifdef`-soup.
- This plan is **P0 only** (the migration). The P4 board env, C6 networking, LVGL display, peripherals, and dual-target OTA are separate plans built on this baseline (see the design spec, §7).
```
