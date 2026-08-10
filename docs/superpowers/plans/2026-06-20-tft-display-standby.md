# TFT Display: Brightness, Timeout & Standby Screen — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add configurable active/standby backlight brightness, a runtime idle timeout, and a dimmed standby screen to the LVGL TFT renderer, surfaced via `/config` like `tft_theme`.

**Architecture:** Replace the on/off `digitalWrite` backlight with LEDC PWM exposed as `lvgl_panel_set_backlight(pct)`. Add three `uint32_t` config keys. Add a dedicated `standby_screen` LVGL screen (ring left + status word, TODAY/TOTAL kWh stacked right) that `LcdTask` switches to when idle past the timeout, dimming the backlight to the standby level; charging/fault states stay full-bright. Pure policy math (`pct→duty`, `should_standby`) lives in a host-tested helper.

**Tech Stack:** C++ / Arduino-ESP32 (core 2.x & 3.x, LEDC), LVGL 8.3, PlatformIO, doctest (native_test env). Gated behind `ENABLE_SCREEN_LVGL_TFT`.

**Branch / worktree:** `feature/tft-display-standby` in `/home/rar/oevse/openevse-master`. All `pio` commands run from there with `~/.platformio/penv/bin/pio`.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/lvgl_tft/backlight.{h,cpp}` (new) | Pure, host-testable policy: `bl_pct_to_duty()`, `bl_should_standby()`. No Arduino/LVGL/openevse deps. |
| `test/test_backlight/test_backlight.cpp` (new) | doctest unit tests for the pure helpers. |
| `src/lvgl_tft/lvgl_panel.{h,cpp}` (modify) | PWM backlight via LEDC; `lvgl_panel_set_backlight(pct)`; gated self-test ramp. |
| `src/app_config.{h,cpp}` (modify) | `tft_brightness`, `tft_standby_brightness`, `tft_timeout` config keys. |
| `src/lvgl_tft/screen_common.{h,cpp}` (new) | Extract `state_word()` + `wifi_percent()` shared by charge + standby screens. |
| `src/lvgl_tft/charge_screen.cpp` (modify) | Use `screen_common`; drop the local static copies. |
| `src/lvgl_tft/standby_screen.{h,cpp}` (new) | The standby LVGL screen + `StandbyScreenData`. |
| `src/lcd_lvgl.{h,cpp}` (modify) | Orchestration: `SCR_STANDBY`, brightness apply, timeout, enter/exit standby, render routing. |
| `platformio.ini` (modify) | `native_test` picks up `backlight.cpp`; remove the now-unused `TFT_BACKLIGHT_TIMEOUT_MS` gate. |
| `docs/superpowers/tft-display-gui-handoff.md` (new) | What the nightshift GUI agent needs to add the controls. |

---

## Task 1: Pure backlight policy helpers (TDD, native)

**Files:**
- Create: `src/lvgl_tft/backlight.h`, `src/lvgl_tft/backlight.cpp`
- Test: `test/test_backlight/test_backlight.cpp`
- Modify: `platformio.ini` (`[env:native_test]`)

- [ ] **Step 1: Add `backlight.cpp` to the native test build + include path**

In `platformio.ini`, edit the `[env:native_test]` block so the filter and flags read:

```ini
build_src_filter = -<*> +<tsdb_sample.cpp> +<home_battery.cpp> +<lvgl_tft/backlight.cpp>
build_flags = -std=gnu++17 -I src/lvgl_tft
```

- [ ] **Step 2: Write the failing test**

Create `test/test_backlight/test_backlight.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "backlight.h"

TEST_CASE("bl_pct_to_duty maps 0..100 to 0..255") {
  CHECK(bl_pct_to_duty(0) == 0);
  CHECK(bl_pct_to_duty(100) == 255);
  CHECK(bl_pct_to_duty(50) == 128);   // (50*255+50)/100 = 128 (rounded)
  CHECK(bl_pct_to_duty(200) == 255);  // over-range clamps
  for (uint8_t p = 1; p <= 100; ++p) {
    CHECK(bl_pct_to_duty(p) >= bl_pct_to_duty((uint8_t)(p - 1)));
  }
}

TEST_CASE("bl_should_standby: keep_awake never sleeps") {
  CHECK_FALSE(bl_should_standby(true, 600, 999999));
}

TEST_CASE("bl_should_standby: timeout 0 means never") {
  CHECK_FALSE(bl_should_standby(false, 0, 999999));
}

TEST_CASE("bl_should_standby: sleeps only once idle reaches the timeout") {
  CHECK_FALSE(bl_should_standby(false, 600, 599999));
  CHECK(bl_should_standby(false, 600, 600000));
  CHECK(bl_should_standby(false, 600, 600001));
}
```

- [ ] **Step 3: Run the test, verify it fails to compile (no `backlight.h`)**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_backlight`
Expected: FAIL — `backlight.h: No such file or directory`.

- [ ] **Step 4: Create the header**

Create `src/lvgl_tft/backlight.h`:

```cpp
#ifndef __LVGL_TFT_BACKLIGHT_H
#define __LVGL_TFT_BACKLIGHT_H

#include <stdint.h>

// Pure, host-testable backlight policy helpers (no Arduino/LVGL/openevse deps).

// Map a 0..100 brightness percentage to an 8-bit LEDC duty (0..255). Clamps >100.
uint8_t bl_pct_to_duty(uint8_t pct);

// Decide whether the panel should drop to standby.
//   keep_awake : caller-computed (charging / fault states force the screen bright)
//   timeout_s  : idle timeout in seconds; 0 == never sleep
//   idle_ms    : milliseconds since the last wake
bool bl_should_standby(bool keep_awake, uint32_t timeout_s, uint32_t idle_ms);

#endif // __LVGL_TFT_BACKLIGHT_H
```

- [ ] **Step 5: Create the implementation**

Create `src/lvgl_tft/backlight.cpp`:

```cpp
// Pure backlight policy. Compiled for both device and native_test; no guards.
#include "backlight.h"

uint8_t bl_pct_to_duty(uint8_t pct)
{
  if (pct >= 100) {
    return 255;
  }
  return (uint8_t)(((uint16_t)pct * 255u + 50u) / 100u);
}

bool bl_should_standby(bool keep_awake, uint32_t timeout_s, uint32_t idle_ms)
{
  if (keep_awake) {
    return false;
  }
  if (timeout_s == 0) {
    return false;
  }
  return idle_ms >= timeout_s * 1000UL;
}
```

- [ ] **Step 6: Run the test, verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_backlight`
Expected: PASS — all assertions in 4 test cases pass.

- [ ] **Step 7: Commit**

```bash
git add src/lvgl_tft/backlight.h src/lvgl_tft/backlight.cpp test/test_backlight/test_backlight.cpp platformio.ini
git commit -m "feat(lcd): pure backlight policy helpers + native tests"
```

---

## Task 2: PWM backlight in lvgl_panel + bench dimmability gate

**Files:**
- Modify: `src/lvgl_tft/lvgl_panel.h`, `src/lvgl_tft/lvgl_panel.cpp`

- [ ] **Step 1: Declare the setter in the header**

In `src/lvgl_tft/lvgl_panel.h`, add after `bool lvgl_panel_begin();`:

```cpp
// Set the backlight brightness, 0..100%. Safe to call before lvgl_panel_begin()
// (no-op until the LEDC channel is attached). Drives active vs. standby dimming.
void lvgl_panel_set_backlight(uint8_t pct);
```

- [ ] **Step 2: Add includes + PWM config defines**

In `src/lvgl_tft/lvgl_panel.cpp`, add to the include block:

```cpp
#include "backlight.h"
```

And after the `#warning` block near the top, add:

```cpp
// Backlight PWM. RGB-LED PWM (LedManagerTask) uses LEDC channels 1..3 and WS2812
// uses RMT, so channel 0 is free for the 2.x ledcAttachPin path.
#ifndef LCD_BL_PWM_FREQ
#define LCD_BL_PWM_FREQ 5000
#endif
#ifndef LCD_BL_PWM_RES
#define LCD_BL_PWM_RES 8
#endif
#ifndef LCD_BL_LEDC_CHANNEL
#define LCD_BL_LEDC_CHANNEL 0
#endif

static bool bl_ready = false;
```

- [ ] **Step 3: Replace the on/off backlight init in `lvgl_panel_begin()`**

In `src/lvgl_tft/lvgl_panel.cpp`, replace these two lines in `lvgl_panel_begin()`:

```cpp
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
```

with:

```cpp
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(TFT_BL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
#else
  ledcSetup(LCD_BL_LEDC_CHANNEL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
  ledcAttachPin(TFT_BL, LCD_BL_LEDC_CHANNEL);
#endif
  bl_ready = true;
  lvgl_panel_set_backlight(100); // full on until LcdTask applies the configured level

#ifdef LCD_BL_PWM_SELFTEST
  // Gated diagnostic: ramp the backlight up/down a few times so a human can
  // confirm the panel actually dims (some BL circuits are on/off-only).
  for (int cycle = 0; cycle < 3; ++cycle) {
    for (int p = 0; p <= 100; p += 5) { lvgl_panel_set_backlight((uint8_t)p); delay(30); }
    for (int p = 100; p >= 0; p -= 5) { lvgl_panel_set_backlight((uint8_t)p); delay(30); }
  }
  lvgl_panel_set_backlight(100);
#endif
```

- [ ] **Step 4: Implement `lvgl_panel_set_backlight()`**

In `src/lvgl_tft/lvgl_panel.cpp`, add before the final `#endif // ENABLE_SCREEN_LVGL_TFT`:

```cpp
void lvgl_panel_set_backlight(uint8_t pct)
{
  if (!bl_ready) {
    return;
  }
  uint8_t duty = bl_pct_to_duty(pct);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(TFT_BL, duty);
#else
  ledcWrite(LCD_BL_LEDC_CHANNEL, duty);
#endif
}
```

- [ ] **Step 5: Build the TFT env to verify it compiles**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: `[SUCCESS]`. (The panel still shows full brightness — `LcdTask` does not drive it yet.)

- [ ] **Step 6: BENCH GATE — flash with the self-test and confirm clean dimming**

> This is the Step-0 hardware gate from the spec. **Do not proceed past this step until a human confirms smooth dimming.** The device is on `/dev/ttyUSB0`; flashing may need the BOOT/EN hand-trigger (see the LVGL display memory note).

Run:
```bash
PLATFORMIO_BUILD_FLAGS=-DLCD_BL_PWM_SELFTEST ~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1 -t upload
```
Expected: on boot the backlight ramps smoothly up and down three times, then settles at full.

- **PASS** (smooth fade, no flicker/whine): continue.
- **FAIL** (no intermediate levels / bad flicker / audible whine): STOP. The board's backlight is not cleanly PWM-dimmable. Report to the user — brightness collapses to on/off and the design needs revisiting (e.g. standby = off only). Do not continue the remaining tasks without a decision.

- [ ] **Step 7: Commit (leaving the gated self-test in place as a diagnostic)**

```bash
git add src/lvgl_tft/lvgl_panel.h src/lvgl_tft/lvgl_panel.cpp
git commit -m "feat(lcd): PWM backlight via LEDC + gated dimmability self-test"
```

---

## Task 3: Brightness + timeout config keys

**Files:**
- Modify: `src/app_config.h`, `src/app_config.cpp`

- [ ] **Step 1: Declare the globals**

In `src/app_config.h`, add after `extern String tft_theme;` (line ~47):

```cpp
extern uint32_t tft_brightness;
extern uint32_t tft_standby_brightness;
extern uint32_t tft_timeout;
```

- [ ] **Step 2: Define the globals**

In `src/app_config.cpp`, add after `String tft_theme;` (line ~70):

```cpp
uint32_t tft_brightness;
uint32_t tft_standby_brightness;
uint32_t tft_timeout;
```

- [ ] **Step 3: Register the config options (gated, next to `tft_theme`)**

In `src/app_config.cpp`, the `tft_theme` option sits inside a `#ifdef ENABLE_SCREEN_LVGL_TFT` block in `opts[]` (line ~209). Add these three lines immediately after the `tft_theme` line, inside the same `#ifdef`:

```cpp
  new ConfigOptDefinition<uint32_t>(tft_brightness, 100, "tft_brightness", "tb"),
  new ConfigOptDefinition<uint32_t>(tft_standby_brightness, 15, "tft_standby_brightness", "tsb"),
  new ConfigOptDefinition<uint32_t>(tft_timeout, 600, "tft_timeout", "tto"),
```

> If `tft_theme` is not already wrapped in `#ifdef ENABLE_SCREEN_LVGL_TFT`, wrap all four lines in one. Verify the short codes `tb`/`tsb`/`tto` are unique with:
> `grep -oE '"[a-z]+"\),?$' src/app_config.cpp | sort | uniq -d` (expect no output).

- [ ] **Step 4: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/app_config.h src/app_config.cpp
git commit -m "feat(config): tft_brightness / tft_standby_brightness / tft_timeout keys"
```

---

## Task 4: Extract shared screen helpers (`screen_common`)

**Files:**
- Create: `src/lvgl_tft/screen_common.h`, `src/lvgl_tft/screen_common.cpp`
- Modify: `src/lvgl_tft/charge_screen.cpp`

- [ ] **Step 1: Create the header**

Create `src/lvgl_tft/screen_common.h`:

```cpp
// Shared helpers for the LVGL TFT screens (charge + standby).
#ifndef __SCREEN_COMMON_H
#define __SCREEN_COMMON_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>
#include <lvgl.h>

// Map an OPENEVSE_STATE_* value to a status word + accent colour (nightshift).
const char *state_word(uint8_t evse_state, lv_color_t *colour);

// RSSI (dBm) -> signal %, the usual piecewise mapping.
int wifi_percent(int rssi);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __SCREEN_COMMON_H
```

- [ ] **Step 2: Create the implementation (moved verbatim from charge_screen.cpp, colours via NS_* macros)**

Create `src/lvgl_tft/screen_common.cpp`:

```cpp
#include "screen_common.h"

#ifdef ENABLE_SCREEN_LVGL_TFT

#include "openevse.h"     // OPENEVSE_STATE_*
#include "nightshift.h"   // NS_* palette macros

const char *state_word(uint8_t s, lv_color_t *colour)
{
  switch (s) {
    case OPENEVSE_STATE_CHARGING:      *colour = NS_SUCCESS; return "CHARGING";
    case OPENEVSE_STATE_CONNECTED:     *colour = NS_ACCENT;  return "CONNECTED";
    case OPENEVSE_STATE_SLEEPING:      *colour = NS_SLEEP;   return "SLEEPING";
    case OPENEVSE_STATE_DISABLED:      *colour = NS_TEXTDIM; return "DISABLED";
    case OPENEVSE_STATE_STARTING:      *colour = NS_ACCENT;  return "STARTING";
    case OPENEVSE_STATE_NOT_CONNECTED: *colour = NS_TEXTDIM; return "NOT CONNECTED";
    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:  *colour = NS_ERROR;   return "FAULT";
    default:                           *colour = NS_TEXTDIM; return "--";
  }
}

int wifi_percent(int rssi)
{
  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;
  return 2 * (rssi + 100);
}

#endif // ENABLE_SCREEN_LVGL_TFT
```

- [ ] **Step 3: Remove the static copies from charge_screen.cpp and include the header**

In `src/lvgl_tft/charge_screen.cpp`:
1. Add `#include "screen_common.h"` after the existing `#include "nightshift.h"`.
2. Delete the two `static` function definitions `state_word(...)` and `wifi_percent(...)` (the block after `charge_screen_build()`, currently ~lines 171–200, from the `// Map EVSE state` comment through the end of `wifi_percent`).

> The `COL_OK`/`COL_ACCENT`/etc. macros that those functions used are defined in `charge_screen.cpp`; `screen_common.cpp` uses the `NS_*` macros directly instead. The colours are identical (`COL_OK`=`NS_SUCCESS`, `COL_FAULT`=`NS_ERROR`, `COL_SLEEP`=`NS_SLEEP`, `COL_ACCENT`=`NS_ACCENT`, `COL_DIM`=`NS_TEXTDIM`).

- [ ] **Step 4: Build to verify the charge screen still links**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: `[SUCCESS]` (no "multiple definition" / "undefined reference" for `state_word`/`wifi_percent`).

- [ ] **Step 5: Commit**

```bash
git add src/lvgl_tft/screen_common.h src/lvgl_tft/screen_common.cpp src/lvgl_tft/charge_screen.cpp
git commit -m "refactor(lcd): share state_word/wifi_percent via screen_common"
```

---

## Task 5: Standby screen

**Files:**
- Create: `src/lvgl_tft/standby_screen.h`, `src/lvgl_tft/standby_screen.cpp`

- [ ] **Step 1: Create the header**

Create `src/lvgl_tft/standby_screen.h`:

```cpp
// src/lvgl_tft/standby_screen.h — dimmed idle screen for the stock TFT.
// Ring + status word (left), TODAY/TOTAL kWh stacked (right), clock + temp/wifi
// in the top corners, host/IP in the bottom corners. Read-only (no touch).
#ifndef __STANDBY_SCREEN_H
#define __STANDBY_SCREEN_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdint.h>

struct StandbyScreenData {
  uint8_t  evse_state;        // OPENEVSE_STATE_* (drives the ring word + colour)
  bool     vehicle_connected;
  bool     temp_valid;
  float    temp_c;
  bool     wifi_client;       // true = STA, false = AP
  bool     wifi_connected;
  int      rssi;              // STA dBm
  int      sta_count;         // AP station count
  double   today_kwh;         // getTotalDay()  (kWh)
  double   total_kwh;         // getTotalEnergy() (kWh)
  const char *clock;          // "9:41 · Fri Jun 20"
  const char *hostname;       // bottom-left
  const char *ip;             // bottom-right
};

void standby_screen_build();
void standby_screen_destroy();
void standby_screen_update(const StandbyScreenData &d);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __STANDBY_SCREEN_H
```

- [ ] **Step 2: Create the implementation**

Create `src/lvgl_tft/standby_screen.cpp`:

```cpp
// src/lvgl_tft/standby_screen.cpp — see standby_screen.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "standby_screen.h"
#include "screen_common.h"   // state_word, wifi_percent
#include "nightshift.h"

#define COL_BG     NS_SURFACE
#define COL_TRACK  NS_BORDER
#define COL_TEXT   NS_TEXT
#define COL_DIM    NS_TEXTDIM
#define COL_ACCENT NS_ACCENT

static lv_obj_t *standby_scr  = nullptr;
static lv_obj_t *arc          = nullptr;
static lv_obj_t *state_lbl    = nullptr;
static lv_obj_t *clock_lbl    = nullptr;
static lv_obj_t *topright_lbl = nullptr;
static lv_obj_t *today_val    = nullptr;
static lv_obj_t *total_val    = nullptr;
static lv_obj_t *host_lbl     = nullptr;
static lv_obj_t *ip_lbl       = nullptr;

void standby_screen_build()
{
  // Load the new screen before deleting the old (active-screen-delete panics LVGL).
  lv_obj_t *old = standby_scr;
  lv_obj_t *scr = lv_obj_create(NULL);
  standby_scr = scr;
  lv_scr_load(scr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Top strip: clock (left), temp + wifi (right).
  clock_lbl = lv_label_create(scr);
  lv_label_set_text(clock_lbl, "");
  lv_obj_set_style_text_color(clock_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(clock_lbl, LV_ALIGN_TOP_LEFT, 12, 8);

  topright_lbl = lv_label_create(scr);
  lv_label_set_text(topright_lbl, "");
  lv_obj_set_style_text_color(topright_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(topright_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(topright_lbl, LV_ALIGN_TOP_RIGHT, -12, 8);

  // Ring on the left (same geometry/style as the charge screen).
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, 200, 200);
  lv_obj_align(arc, LV_ALIGN_LEFT_MID, 18, 8);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, COL_ACCENT, LV_PART_INDICATOR);

  state_lbl = lv_label_create(scr);
  lv_label_set_long_mode(state_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(state_lbl, 180);
  lv_obj_set_style_text_align(state_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(state_lbl, "");
  lv_obj_set_style_text_color(state_lbl, COL_ACCENT, 0);
  lv_obj_set_style_text_font(state_lbl, &lv_font_montserrat_28, 0);
  lv_obj_align_to(state_lbl, arc, LV_ALIGN_CENTER, 0, 0);

  // Totals stacked + right-aligned on the right half.
  lv_obj_t *today_t = lv_label_create(scr);
  lv_label_set_text(today_t, "TODAY");
  lv_obj_set_style_text_color(today_t, COL_DIM, 0);
  lv_obj_set_style_text_font(today_t, &lv_font_montserrat_14, 0);
  lv_obj_align(today_t, LV_ALIGN_RIGHT_MID, -24, -56);

  today_val = lv_label_create(scr);
  lv_label_set_text(today_val, "-- kWh");
  lv_obj_set_style_text_color(today_val, COL_ACCENT, 0);
  lv_obj_set_style_text_font(today_val, &lv_font_montserrat_28, 0);
  lv_obj_align(today_val, LV_ALIGN_RIGHT_MID, -24, -28);

  lv_obj_t *total_t = lv_label_create(scr);
  lv_label_set_text(total_t, "TOTAL");
  lv_obj_set_style_text_color(total_t, COL_DIM, 0);
  lv_obj_set_style_text_font(total_t, &lv_font_montserrat_14, 0);
  lv_obj_align(total_t, LV_ALIGN_RIGHT_MID, -24, 24);

  total_val = lv_label_create(scr);
  lv_label_set_text(total_val, "-- kWh");
  lv_obj_set_style_text_color(total_val, COL_TEXT, 0);
  lv_obj_set_style_text_font(total_val, &lv_font_montserrat_28, 0);
  lv_obj_align(total_val, LV_ALIGN_RIGHT_MID, -24, 52);

  // Bottom corners: hostname (left), IP (right).
  host_lbl = lv_label_create(scr);
  lv_label_set_text(host_lbl, "");
  lv_obj_set_style_text_color(host_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(host_lbl, LV_ALIGN_BOTTOM_LEFT, 12, -6);

  ip_lbl = lv_label_create(scr);
  lv_label_set_text(ip_lbl, "");
  lv_obj_set_style_text_color(ip_lbl, COL_DIM, 0);
  lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(ip_lbl, LV_ALIGN_BOTTOM_RIGHT, -12, -6);

  if (old) {
    lv_obj_del(old);
  }
}

void standby_screen_update(const StandbyScreenData &d)
{
  char buf[48];

  lv_color_t accent;
  const char *word = state_word(d.evse_state, &accent);
  lv_label_set_text(state_lbl, word);
  lv_obj_set_style_text_color(state_lbl, accent, 0);
  lv_obj_align_to(state_lbl, arc, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);

  lv_label_set_text(clock_lbl, d.clock ? d.clock : "");

  char tr[48]; tr[0] = '\0';
  size_t n = 0;
  if (d.temp_valid) n += snprintf(tr + n, sizeof(tr) - n, "%.1fC  ", d.temp_c);
  if (d.wifi_client) {
    if (d.wifi_connected) n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " %d%%", wifi_percent(d.rssi));
    else                  n += snprintf(tr + n, sizeof(tr) - n, LV_SYMBOL_WIFI " --");
  } else {
    n += snprintf(tr + n, sizeof(tr) - n, "AP:%d", d.sta_count);
  }
  lv_label_set_text(topright_lbl, tr);

  snprintf(buf, sizeof(buf), "%.1f kWh", d.today_kwh);
  lv_label_set_text(today_val, buf);
  snprintf(buf, sizeof(buf), "%.0f kWh", d.total_kwh);
  lv_label_set_text(total_val, buf);

  lv_label_set_text(host_lbl, d.hostname ? d.hostname : "");
  lv_label_set_text(ip_lbl, d.ip ? d.ip : "");
}

void standby_screen_destroy()
{
  if (standby_scr) {
    lv_obj_del(standby_scr);
    standby_scr  = nullptr;
    arc = state_lbl = clock_lbl = topright_lbl = nullptr;
    today_val = total_val = host_lbl = ip_lbl = nullptr;
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
```

- [ ] **Step 3: Build to verify it compiles (not yet wired in)**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add src/lvgl_tft/standby_screen.h src/lvgl_tft/standby_screen.cpp
git commit -m "feat(lcd): standby screen (ring + status, today/total kWh)"
```

---

## Task 6: Wire standby + brightness into LcdTask

**Files:**
- Modify: `src/lcd_lvgl.h`, `src/lcd_lvgl.cpp`, `platformio.ini`

- [ ] **Step 1: Replace the backlight members in the header**

In `src/lcd_lvgl.h`, replace this block:

```cpp
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    uint32_t _backlight_timeout = 0;
    uint8_t  _prev_state = 0xff;
    bool     _prev_vehicle = false;
    void wakeBacklight();
    void updateBacklight();
#endif
```

with:

```cpp
    // Backlight + standby (PWM). Brightness 0..100; idle measured from _lastWake.
    uint32_t _lastWake = 0;
    uint8_t  _prev_state = 0xff;
    bool     _prev_vehicle = false;
    bool     _standby = false;          // currently dimmed to the standby screen/level
    int32_t  _activeBrightness = -1;    // cached config (-1 = not read yet)
    int32_t  _standbyBrightness = -1;
    int32_t  _timeoutS = -1;
    void wakeBacklight();               // active brightness, exit standby, re-arm idle
    void enterStandby();                // standby brightness (+ standby screen if >0)
    bool stateKeepsAwake(uint8_t state, double amps);  // charging/fault force-bright
    void applyDisplayConfig();          // refresh cached brightness/timeout + apply live
```

- [ ] **Step 2: Add includes + the SCR_STANDBY define**

In `src/lcd_lvgl.cpp`:
1. Add to the include block (after `#include "lvgl_tft/charge_screen.h"`):
```cpp
#include "lvgl_tft/standby_screen.h"
#include "lvgl_tft/backlight.h"
```
2. Delete the `#ifndef LCD_BACKLIGHT_PIN ... #endif` block (lines ~23–25) — the pin is owned by the PWM panel now.
3. Add to the `_activeScreen` defines:
```cpp
#define SCR_STANDBY 3
```

- [ ] **Step 3: Make message + wifi-mode wakes unconditional**

In `src/lcd_lvgl.cpp`:
- In `displayNextMessage()`, replace:
```cpp
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
    wakeBacklight();
#endif
```
with:
```cpp
    wakeBacklight();
```
- In `setWifiMode()`, replace:
```cpp
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  wakeBacklight();
#endif
```
with:
```cpp
  wakeBacklight();
```

- [ ] **Step 4: Update the init block**

In `src/lcd_lvgl.cpp` `loop()`, replace the panel-up branch body:

```cpp
    if(_displayOk) {
      applyThemeFromConfig();  // pick the palette before the first screen is built
      boot_screen_build();
      _booting = true;
      _bootStart = millis();
      pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
      wakeBacklight();
#else
      digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
#endif
    }
```

with:

```cpp
    if(_displayOk) {
      applyThemeFromConfig();  // pick the palette before the first screen is built
      applyDisplayConfig();    // cache brightness/timeout before the first wake
      boot_screen_build();
      _booting = true;
      _bootStart = millis();
      wakeBacklight();         // light the splash at the active brightness
    }
```

- [ ] **Step 5: Handle standby in the wifi-mode switch + theme rebuild**

In `src/lcd_lvgl.cpp` `loop()`, the wifi-mode switch block currently starts:

```cpp
  bool wantSetup = _wifiModeKnown && !_wifi_client;
  if(wantSetup && _activeScreen == SCR_CHARGE) {
```

Replace that block (through the theme rebuild block) with:

```cpp
  bool wantSetup = _wifiModeKnown && !_wifi_client;
  // The setup screen owns the whole display; never run standby in AP mode.
  if(wantSetup && _standby) {
    wakeBacklight();  // exits standby, rebuilds the charge screen, so the swap below works
  }
  if(wantSetup && _activeScreen == SCR_CHARGE) {
    buildSetupScreen();
    charge_screen_destroy();
    _activeScreen = SCR_SETUP;
  } else if(!wantSetup && _activeScreen == SCR_SETUP) {
    charge_screen_build();
    setup_screen_destroy();
    _activeScreen = SCR_CHARGE;
  }

  // Live theme switch: swap palette + rebuild whichever screen is showing.
  if(applyThemeFromConfig()) {
    if(_activeScreen == SCR_CHARGE) {
      charge_screen_build();
    } else if(_activeScreen == SCR_SETUP) {
      buildSetupScreen();
    } else if(_activeScreen == SCR_STANDBY) {
      standby_screen_build();
    }
  }
```

- [ ] **Step 6: Add the backlight/standby decision + render routing**

In `src/lcd_lvgl.cpp` `loop()`, find the charge-data section that begins:

```cpp
  // Assemble a full snapshot from EvseManager + WiFi + clock.
  ChargeScreenData d = {};
  uint8_t state = _evse->getEvseState();
```

Insert the following **immediately before** that comment:

```cpp
  // --- Backlight / standby decision (chooses which screen we render) ---
  uint8_t state = _evse->getEvseState();
  bool vehicle = _evse->isVehicleConnected();
  applyDisplayConfig();   // pick up live /config changes to brightness/timeout

  if(_prev_state != state || _prev_vehicle != vehicle) {
    wakeBacklight();      // any state change -> full brightness, exit standby, re-arm
    _prev_state = state;
    _prev_vehicle = vehicle;
  }

  bool keepAwake = stateKeepsAwake(state, _evse->getAmps());
  if(keepAwake) {
    _lastWake = millis(); // keep re-arming so we never time out while charging/fault
    if(_standby) {
      wakeBacklight();
    }
  } else if(bl_should_standby(false, (uint32_t)_timeoutS, millis() - _lastWake)) {
    if(!_standby) {
      enterStandby();
    }
  }

  // Render the standby screen when dimmed-with-screen; otherwise fall through to charge.
  if(_activeScreen == SCR_STANDBY) {
    StandbyScreenData sd = {};
    sd.evse_state        = state;
    sd.vehicle_connected = vehicle;
    sd.temp_valid        = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR);
    sd.temp_c            = sd.temp_valid ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0.0f;
    sd.wifi_client       = _wifi_client;
    sd.wifi_connected    = _wifi_connected;
    sd.rssi              = WiFi.RSSI();
    sd.sta_count         = WiFi.softAPgetStationNum();
    sd.today_kwh         = _evse->getTotalDay();
    sd.total_kwh         = _evse->getTotalEnergy();

    char ck[40];
    timeval tv; gettimeofday(&tv, NULL);
    struct tm ti; localtime_r(&tv.tv_sec, &ti);
    char hm[8]; strftime(hm, sizeof(hm), "%I:%M", &ti);
    const char *hmp = (hm[0] == '0') ? hm + 1 : hm;  // "09:41" -> "9:41"
    char dm[16]; strftime(dm, sizeof(dm), "%a %b %d", &ti);
    snprintf(ck, sizeof(ck), "%s \xC2\xB7 %s", hmp, dm);  // · = U+00B7
    sd.clock = ck;

    char ipbuf[20];
    IPAddress ip = _wifi_client ? WiFi.localIP() : WiFi.softAPIP();
    snprintf(ipbuf, sizeof(ipbuf), "%s", ip.toString().c_str());
    sd.hostname = esp_hostname.c_str();
    sd.ip = ipbuf;

    standby_screen_update(sd);
    lv_timer_handler();
    gettimeofday(&tv, NULL);
    return 1000 - tv.tv_usec / 1000;
  }
```

- [ ] **Step 7: Remove the obsolete charge-path backlight block and stale `state` redeclaration**

In `src/lcd_lvgl.cpp`, in the charge-data section:
1. Change `uint8_t state = _evse->getEvseState();` (now duplicated) to reuse the earlier value — replace that line with: `d.evse_state = state;` and delete the now-redundant `d.evse_state = state;` that followed it (keep exactly one).
2. Change `d.vehicle_connected = _evse->isVehicleConnected();` to `d.vehicle_connected = vehicle;`.
3. Delete this block after `charge_screen_update(d);`:
```cpp
#ifdef TFT_BACKLIGHT_TIMEOUT_MS
  if(_prev_state != state || _prev_vehicle != d.vehicle_connected) {
    wakeBacklight();
    _prev_state = state;
    _prev_vehicle = d.vehicle_connected;
  } else {
    updateBacklight();
  }
#endif
```

- [ ] **Step 8: Replace the method implementations at the bottom of the file**

In `src/lcd_lvgl.cpp`, replace the entire `#ifdef TFT_BACKLIGHT_TIMEOUT_MS ... #endif` block (the old `wakeBacklight()` + `updateBacklight()` definitions) with:

```cpp
void LcdTask::applyDisplayConfig()
{
  _activeBrightness  = (int32_t)tft_brightness;
  _standbyBrightness = (int32_t)tft_standby_brightness;
  _timeoutS          = (int32_t)tft_timeout;
  if(_activeBrightness < 10) _activeBrightness = 10;  // never black out the active screen
  // Apply live so brightness-slider changes take effect without waiting for a wake.
  lvgl_panel_set_backlight((uint8_t)(_standby ? (_standbyBrightness < 0 ? 0 : _standbyBrightness)
                                              : (_activeBrightness  < 0 ? 100 : _activeBrightness)));
}

void LcdTask::wakeBacklight()
{
  _lastWake = millis();
  if(_activeBrightness < 0) _activeBrightness = (int32_t)tft_brightness;  // boot safety
  lvgl_panel_set_backlight((uint8_t)_activeBrightness);
  if(_standby) {
    _standby = false;
    if(_activeScreen == SCR_STANDBY) {
      charge_screen_build();
      standby_screen_destroy();
      _activeScreen = SCR_CHARGE;
    }
  }
}

void LcdTask::enterStandby()
{
  _standby = true;
  if(_standbyBrightness > 0) {
    standby_screen_build();
    if(_activeScreen == SCR_CHARGE) {
      charge_screen_destroy();
    }
    _activeScreen = SCR_STANDBY;
  }
  lvgl_panel_set_backlight((uint8_t)(_standbyBrightness < 0 ? 0 : _standbyBrightness));
}

bool LcdTask::stateKeepsAwake(uint8_t state, double amps)
{
  if(!_evse->isVehicleConnected()) {
    return false;
  }
  switch(state) {
    case OPENEVSE_STATE_STARTING:
    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:
      return true;
    case OPENEVSE_STATE_CHARGING:
#ifdef TFT_BACKLIGHT_CHARGING_THRESHOLD
      return amps >= TFT_BACKLIGHT_CHARGING_THRESHOLD;
#else
      return true;
#endif
    default:
      return false;
  }
}
```

- [ ] **Step 9: Drop the now-unused timeout define**

In `platformio.ini`, delete the line `-D TFT_BACKLIGHT_TIMEOUT_MS=600000` (the default now lives in the `tft_timeout` config default of 600s). Leave `-D TFT_BACKLIGHT_CHARGING_THRESHOLD=0.1` (still used by `stateKeepsAwake`).

- [ ] **Step 10: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1`
Expected: `[SUCCESS]`, no warnings about unused `updateBacklight`/`LCD_BACKLIGHT_PIN`/`TFT_BACKLIGHT_TIMEOUT_MS`.

- [ ] **Step 11: Commit**

```bash
git add src/lcd_lvgl.h src/lcd_lvgl.cpp platformio.ini
git commit -m "feat(lcd): idle standby screen + runtime brightness/timeout"
```

---

## Task 7: Full hardware validation

**Files:** none (validation only). Device on `/dev/ttyUSB0`, env `openevse_wifi_tft_v1`.

- [ ] **Step 1: Flash the integrated firmware (no self-test flag)**

Run: `~/.platformio/penv/bin/pio run -e openevse_wifi_tft_v1 -t upload`
Expected: `[SUCCESS]`, hash verified, device resets. (Retry with the BOOT/EN hand-trigger if "No serial data received".)

- [ ] **Step 2: Verify the bench checklist (have the user confirm on the panel)**

- Boot splash shows at active brightness; hands off to the charge screen.
- Leave idle (no vehicle) for the timeout (set `tft_timeout` low, e.g. 15s, via the web UI `/config` POST to speed this up): screen switches to the standby screen (ring left, TODAY/TOTAL right, clock) and the backlight dims to the standby level.
- Plug in / change state: screen wakes to full brightness and returns to the charge screen.
- `POST /config {"tft_brightness":40}` while awake: brightness drops live. `{"tft_standby_brightness":0}` then idle: panel goes fully dark (no standby screen). `{"tft_timeout":0}`: never sleeps.
- Theme switch (`{"tft_theme":"light"}`) while on the standby screen: standby screen repaints in the light palette, no reboot.
- During charging: stays full-bright and on the charge screen regardless of timeout.

- [ ] **Step 3: Commit any tuning** (e.g. default standby %, font sizes, ring geometry) discovered on the bench, with a message describing the adjustment. If nothing changed, skip.

---

## Task 8: GUI handoff doc

**Files:**
- Create: `docs/superpowers/tft-display-gui-handoff.md`

- [ ] **Step 1: Write the handoff doc**

Create `docs/superpowers/tft-display-gui-handoff.md`:

```markdown
# TFT display controls — GUI handoff (nightshift UI agent)

The firmware exposes three new config keys (LVGL-TFT builds only). Read/write them
via the same `/config` GET/POST the theme control already uses. **Presence of the
keys in `GET /config` is the capability signal** — show the controls only when present
(same contract as `tft_theme`).

| Key | Type | Range | Default | Control |
|---|---|---|---|---|
| `tft_brightness` | int | 10–100 | 100 | Active brightness slider (%) |
| `tft_standby_brightness` | int | 0–100 | 15 | Standby brightness slider (%); 0 = screen off |
| `tft_timeout` | int (seconds) | 0, or 5–3600 | 600 | Idle timeout; 0 = "Never sleep" (slider + Never, or presets) |

POST example: `POST /config {"tft_brightness":60,"tft_standby_brightness":10,"tft_timeout":300}`.

Place these in the existing **Display** section alongside the theme selector. Changes
apply live on the device (no reboot).
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/tft-display-gui-handoff.md
git commit -m "docs: GUI handoff for TFT brightness/timeout/standby controls"
```

---

## Notes for the executor

- **Task 2 Step 6 is a hard gate.** If the backlight doesn't dim cleanly, stop and escalate — the rest of the plan assumes PWM dimming works.
- LEDC API differs by core version; the `ESP_ARDUINO_VERSION >= 3.0.0` guard (mirrored from `LedManagerTask.cpp`) handles both. Channel 0 is free (RGB uses 1–3, WS2812 uses RMT).
- The active-screen-delete panic rule (build/load new screen before deleting old) applies to every screen transition — `enterStandby`/`wakeBacklight` follow it.
- Flashing this bench board has a flaky auto-reset; retry uploads with the BOOT/EN hand-trigger.
