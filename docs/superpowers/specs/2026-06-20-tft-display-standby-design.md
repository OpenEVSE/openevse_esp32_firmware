# TFT Display: Brightness, Timeout & Standby Screen — Design

**Date:** 2026-06-20
**Status:** Approved design (pre-plan)
**Branch base:** `master` (post-upstream-merge; contains the LVGL TFT renderer + theme)
**Scope gate:** `ENABLE_SCREEN_LVGL_TFT` only. The legacy TFT_eSPI renderer (Elecrow) is untouched and keeps its on/off backlight.

---

## Goal

Give the LVGL TFT display three user-facing controls — **active brightness**, **idle timeout**, and a **dimmed standby screen** — surfaced in the existing "display" section of the web UI (same plumbing as `tft_theme`). On idle timeout, instead of blanking the backlight, the panel dims to a configurable level and shows a calm standby screen that keeps the charge screen's visual language.

## Approach

Approach 1 of the brainstorm: a **dedicated standby LVGL screen** + a **PWM backlight helper**, reusing the existing screen-state machine and keep-awake logic. Gated entirely behind `ENABLE_SCREEN_LVGL_TFT`.

**Step 0 (gating, do first): PWM bench-check.** Before any feature code, confirm GPIO27 (`TFT_BL`) backlight dims cleanly under LEDC PWM on the bench board. Some BL circuits are on/off-only. If dimming is not clean (flicker/whine/no effect at intermediate duty), stop and revisit — brightness would collapse to on/off and the standby-dim concept needs rethinking.

---

## Behavioral model

States (the panel has **no touch**; all wake is event-driven):

- **Active** — screen at `tft_brightness`, showing the charge (or setup) screen. Entered on boot and on any EVSE state change / display event.
- **Standby** — screen at `tft_standby_brightness`, showing the standby screen. Entered when *idle* and the idle timeout has elapsed.

**Keep-awake (never enter standby):** charging and fault states keep the screen Active indefinitely — this is the *existing* `updateBacklight()` rule, preserved verbatim:
`STARTING, VENT_REQUIRED, DIODE_CHECK_FAILED, GFI_FAULT, NO_EARTH_GROUND, STUCK_RELAY, GFI_SELF_TEST_FAILED, OVER_TEMPERATURE, OVER_CURRENT`, and `CHARGING` (gated by `TFT_BACKLIGHT_CHARGING_THRESHOLD` if defined).

**Wake:** any EVSE state change / display update calls `wakeBacklight()` → returns to Active (charge/setup screen + active brightness) and re-arms the timeout.

**Timeout = 0 → never sleep:** stays Active forever.

**`tft_standby_brightness` = 0 → fully off:** behaves like the legacy blank-on-timeout. In this case we **skip loading the standby screen** (just set backlight to 0) — no point rendering to a black panel, and it avoids the screen rebuild.

---

## Config schema

Three new `ConfigOptDefinition<...>` entries in `app_config.cpp` `opts[]`, all gated `#ifdef ENABLE_SCREEN_LVGL_TFT` (mirrors how `tft_theme` is gated), with `extern` decls + global defs in `app_config.h`/`.cpp`:

| Config key | Short | Type | Range | Default | Meaning |
|---|---|---|---|---|---|
| `tft_brightness` | `tb` | uint | 10–100 | 100 | Active backlight % |
| `tft_standby_brightness` | `tsb` | uint | 0–100 | 15 | Standby backlight % (0 = off) |
| `tft_timeout` | `tto` | uint | 0, or 5–3600 | 600 | Idle seconds before standby; 0 = never |

Notes:
- Active min clamped at 10% so the user can't lock themselves into a black *active* screen.
- Short codes must be checked for collisions against the existing `opts[]` at implementation time; adjust if taken.
- Stored as numbers; the firmware just consumes the value. The GUI decides slider vs. dropdown.
- **Capability signal for the GUI:** presence of these keys in `/config` (only on LVGL-TFT builds) tells the UI to show the controls — same contract as `tft_theme`.

---

## Components / file structure

**`src/lvgl_tft/lvgl_panel.{h,cpp}` (modify)** — replace the on/off backlight with PWM.
- New API: `void lvgl_panel_set_backlight(uint8_t pct);` (0–100 → LEDC duty).
- Init: `ledcAttach(TFT_BL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES)` (ESP32 Arduino core 3.x API; project is on core 3.x). `LCD_BL_PWM_FREQ = 5000` Hz, `LCD_BL_PWM_RES = 8` bits (duty 0–255). Active-high (GPIO27 HIGH = on today), so `duty = round(pct * 255 / 100)`.
- Replaces `pinMode/digitalWrite(TFT_BL, HIGH)` in `lvgl_panel_begin()`.
- Verify no LEDC channel conflict on `openevse_wifi_tft_v1` (WS2812 etc. use RMT, not LEDC, but confirm).

**`src/lvgl_tft/backlight.{h,cpp}` (new, small, host-testable pure helper)** — extract the pure decision logic so it can be unit-tested natively (consistent with the `vehicle_extras` native-test pattern):
- `uint8_t bl_pct_to_duty(uint8_t pct);` — pct→8-bit duty mapping.
- `bool bl_should_standby(bool vehicle_connected, uint8_t evse_state, double amps, double charging_threshold, uint32_t idle_ms, uint32_t timeout_ms);` — returns true when the panel should be in standby. Encapsulates the keep-awake state list + timeout==0 (never) logic. `lcd_lvgl` calls this instead of inlining the switch.

**`src/lvgl_tft/standby_screen.{h,cpp}` (new)** — dedicated LVGL screen, same pattern as `charge_screen`/`setup_screen` (owns its `lv_obj`, build-load-then-delete-old, `_destroy()`):
- `void standby_screen_build();` and `void standby_screen_update(const DisplayData &d);` and `void standby_screen_destroy();`
- Layout = **direction B + totals** (see below).
- Uses the `NS_*` nightshift palette macros so it tracks the theme automatically.

**`src/lcd_lvgl.{h,cpp}` (modify)** — the orchestrator:
- Add `SCR_STANDBY` to the screen-state enum (joins `SCR_BOOT/SCR_SETUP/SCR_CHARGE`).
- `wakeBacklight()`: set backlight to `tft_brightness`; if currently `SCR_STANDBY`, rebuild the charge/setup screen and set `_activeScreen` back; re-arm `_backlight_timeout` using `tft_timeout`.
- `updateBacklight()`: replace the hardcoded `TFT_BACKLIGHT_TIMEOUT_MS` macro path with `tft_timeout`-driven logic via `bl_should_standby(...)`. On entering standby: if `tft_standby_brightness > 0`, build+load the standby screen and set standby backlight; else (== 0) set backlight 0 and leave the screen as-is (legacy blank).
- Periodic 1 Hz update routes `standby_screen_update()` when `_activeScreen == SCR_STANDBY`.
- Apply `tft_brightness` at boot (after panel begin) like the theme is applied at boot.
- Poll config each loop for live changes to the three keys (same approach the theme uses), applying brightness/timeout immediately.

**`src/app_config.{h,cpp}` (modify)** — the three config keys above.

**`platformio.ini` (modify)** — `TFT_BACKLIGHT_TIMEOUT_MS` is now just the *default* feeding `tft_timeout`'s default (600s) — keep or fold into the config default. `TFT_BL=27` unchanged. Add `LCD_BL_PWM_FREQ`/`LCD_BL_PWM_RES` defines (or hardcode in `lvgl_panel.cpp`).

---

## Standby screen layout (direction B + totals)

480×320 landscape, nightshift palette, physically dimmed via backlight. **Two-column layout (approved via mockup):**

- **Left half — status ring** (outline only, no fill) with the **status word** inside — e.g. `Ready` (accent) and a sub-line `not connected` / `connected` (dim). Ring placement mirrors the charge screen; ring color tracks EVSE state (teal ready, green connected/complete, amber/red fault).
- **Right half — totals, stacked and right-aligned**, vertically centered on the ring:
  - **TODAY** ← `EvseManager::getTotalDay()` (kWh), accent
  - **TOTAL** ← `EvseManager::getTotalEnergy()` (kWh)
  - Large numerals (room to be bigger than the charge-screen tiles since the right half is theirs).
- **Top-left:** clock — `9:41 · Fri Jun 20`.
- **Top-right:** `24°C  📶 86%` (temp + wifi%).
- **Bottom corners:** hostname (left) + IP (right), faint.

`DisplayData` (the struct the charge screen already receives — it carries `session_wh` today) gains two fields, `today_wh` and `total_wh`, populated by `lcd_lvgl` from `getTotalDay()/getTotalEnergy()`. `standby_screen_update(const DisplayData &d)` reads them — no direct `EvseManager` access from the screen.

---

## State transitions

```
BOOT ──(wifi mode known)──► SETUP (AP) │ CHARGE (client)
CHARGE/SETUP ──(idle ≥ tft_timeout, not keep-awake)──► STANDBY
STANDBY ──(any state change / event / wake)──► CHARGE (or SETUP)
keep-awake states (charging/fault) ──► never leave Active
tft_timeout == 0 ──► never enter STANDBY
tft_standby_brightness == 0 ──► STANDBY = backlight off, no screen swap
```

Transitions always **build + `lv_scr_load` the new screen before deleting the old** — the active-screen-delete panic rule from the theme work (`bc0c6fd`) applies.

---

## Testing

**Native unit tests** (`test/test_backlight/`, doctest, like `test_vehicle_extras`):
- `bl_pct_to_duty`: 0→0, 100→255, 50→~128, monotonic.
- `bl_should_standby`: keep-awake states return false regardless of idle; timeout==0 returns false; idle<timeout false; idle≥timeout & idle-eligible true.

**Hardware (bench, `/dev/ttyUSB0`, `openevse_wifi_tft_v1`):**
- **Step 0:** PWM dimming spike — drive GPIO27 across duty levels, confirm smooth dim, no flicker/whine. **Gates the rest.**
- Full build + flash; verify: active brightness change from web UI takes effect live; timeout → standby screen at dim level; plug/unplug + charging keep-awake; `tft_timeout=0` never sleeps; `tft_standby_brightness=0` blanks; theme switch still repaints standby screen.

---

## Risks / open items

- **PWM dimmability** (gated by Step 0). Primary risk.
- **LEDC channel/freq:** confirm no conflict on this env; tune frequency if any audible whine or visible flicker.
- **Flash budget:** new screen + helper adds a little code/BSS; the LVGL static pool already sits tight on the 4 MB envs, but `openevse_wifi_tft_v1` is a 16 MB/leaner base (~33% flash) — headroom is fine.

## Out of scope (YAGNI)

- Touch-to-wake (no touch on this panel).
- Web-initiated wake / "wake the screen" button.
- Scheduled / auto day-night brightness.
- Brightness/standby for the legacy TFT_eSPI (Elecrow) renderer.
