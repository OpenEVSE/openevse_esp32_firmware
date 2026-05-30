# P4 Display D3: EVSE UI data model + command sink — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`).

**Goal:** Introduce the two app↔UI seams from design spec §6 — `IEvseUiModel` (read-only view of EVSE/network state, seam #1) and `IEvseUiCommandSink` (user intents, seam #2) — with concrete implementations over `EvseManager`/`ManualOverride`, and wire them into the P4 display so the bring-up screen shows **live** EVSE values and the button issues a real start/stop intent.

**Architecture:** Pure-virtual interfaces decouple the LVGL UI from the OpenEVSE app objects. `EvseUiModel` wraps `EvseManager&` (and reads `WiFi` for network status); `EvseUiCommandSink` wraps `ManualOverride&`. `DisplayP4Task` gains references to both: it polls the model each `loop()` to refresh on-screen labels, and routes the button press to `commandSink.toggleCharge()`. D4 will replace the bring-up screen with real portrait charge/lock/fault screens built on the same model + sink.

**Scope/placement note:** For now these live in `src/display_p4/`, guarded by `ENABLE_SCREEN_LVGL`, so D3 stays P4-only and low-risk. They are designed to be *promotable* to a neutral `src/ui/` later if/when the "share UI code with the legacy LVGL effort" decision is made (spec §6, currently deferred). No behavior depends on the placement.

**No-controller caveat:** With no OpenEVSE controller wired, the model reads a disconnected EVSE (state NOT_CONNECTED, ~0 V/A/W). That still exercises the full read path; the on-screen values updating (even at 0) proves the plumbing. `toggleCharge()` exercises `ManualOverride` safely (no controller → effectively a no-op claim, must not crash).

**Builds on D2** (commit 28205b7). Verified accessor facts: `EvseManager` has `getEvseState()` (uint8_t OPENEVSE_STATE_*), `isVehicleConnected()`, `isCharging()`, `isActive()`, `isError()`, `getVoltage()`, `getAmps()`, `getPower()`, `getChargeCurrent()` (pilot A), `getSessionElapsed()`, `getSessionEnergy()`, `getTemperature(EVSE_MONITOR_TEMP_MONITOR)` + `isTemperatureValid(...)`. `ManualOverride` has `toggle()`, `claim(EvseProperties&)`, `release()`, `isActive()`. `EvseProperties` has `setState(EvseState)` + `setChargeCurrent(uint32_t)`; `EVSE_MONITOR_TEMP_MONITOR == 0`.

---

## File Structure

- Create `src/display_p4/evse_ui_model.h` / `.cpp` — `IEvseUiModel` + `EvseUiModel`.
- Create `src/display_p4/evse_ui_command.h` / `.cpp` — `IEvseUiCommandSink` + `EvseUiCommandSink`.
- Modify `src/display_p4/display_p4.h` / `.cpp` — `begin(EvseManager&, ManualOverride&)`; live label refresh; button→intent.
- Modify `src/main.cpp` — `displayP4.begin(evse, manual);`.

---

## Task 1: IEvseUiModel + IEvseUiCommandSink (interfaces + implementations)

**Files:** Create `evse_ui_model.h/.cpp`, `evse_ui_command.h/.cpp` under `src/display_p4/`.

- [ ] **Step 1: `src/display_p4/evse_ui_model.h`**
```cpp
#ifndef DISPLAY_P4_EVSE_UI_MODEL_H
#define DISPLAY_P4_EVSE_UI_MODEL_H
#if defined(ENABLE_SCREEN_LVGL)

#include <stdint.h>

// Read-only view of EVSE + network state for the on-device UI (design spec
// seam #1). Decouples the LVGL screens from EvseManager/EvseMonitor.
class IEvseUiModel
{
public:
  virtual ~IEvseUiModel() {}

  // Charge / connection state
  virtual uint8_t evseState() = 0;        // OPENEVSE_STATE_*
  virtual const char *stateText() = 0;    // human-readable state
  virtual bool vehicleConnected() = 0;
  virtual bool charging() = 0;
  virtual bool active() = 0;               // EVSE enabled (not disabled)
  virtual bool error() = 0;

  // Live electrical values
  virtual double voltage() = 0;            // V
  virtual double amps() = 0;               // A
  virtual double power() = 0;              // W
  virtual uint32_t pilotCurrent() = 0;     // A (pilot / charge-current limit)

  // Session
  virtual uint32_t sessionElapsed() = 0;   // seconds
  virtual double sessionEnergy() = 0;      // Wh

  // Environment
  virtual bool tempValid() = 0;
  virtual double temperatureC() = 0;

  // Network
  virtual bool wifiConnected() = 0;
  virtual bool wifiApMode() = 0;
  virtual int wifiRssi() = 0;              // dBm (STA)
};

class EvseManager;

class EvseUiModel : public IEvseUiModel
{
public:
  explicit EvseUiModel(EvseManager &evse);

  uint8_t evseState() override;
  const char *stateText() override;
  bool vehicleConnected() override;
  bool charging() override;
  bool active() override;
  bool error() override;
  double voltage() override;
  double amps() override;
  double power() override;
  uint32_t pilotCurrent() override;
  uint32_t sessionElapsed() override;
  double sessionEnergy() override;
  bool tempValid() override;
  double temperatureC() override;
  bool wifiConnected() override;
  bool wifiApMode() override;
  int wifiRssi() override;

private:
  EvseManager &_evse;
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_EVSE_UI_MODEL_H
```

- [ ] **Step 2: `src/display_p4/evse_ui_model.cpp`**
```cpp
#if defined(ENABLE_SCREEN_LVGL)

#include <WiFi.h>
#include "evse_man.h"
#include "evse_monitor.h"
#include "openevse.h"
#include "evse_ui_model.h"

EvseUiModel::EvseUiModel(EvseManager &evse) : _evse(evse) {}

uint8_t EvseUiModel::evseState()      { return _evse.getEvseState(); }
bool EvseUiModel::vehicleConnected()  { return _evse.isVehicleConnected(); }
bool EvseUiModel::charging()          { return _evse.isCharging(); }
bool EvseUiModel::active()            { return _evse.isActive(); }
bool EvseUiModel::error()             { return _evse.isError(); }
double EvseUiModel::voltage()         { return _evse.getVoltage(); }
double EvseUiModel::amps()            { return _evse.getAmps(); }
double EvseUiModel::power()           { return _evse.getPower(); }
uint32_t EvseUiModel::pilotCurrent()  { return _evse.getChargeCurrent(); }
uint32_t EvseUiModel::sessionElapsed(){ return _evse.getSessionElapsed(); }
double EvseUiModel::sessionEnergy()   { return _evse.getSessionEnergy(); }
bool EvseUiModel::tempValid()         { return _evse.isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR); }
double EvseUiModel::temperatureC()    { return _evse.getTemperature(EVSE_MONITOR_TEMP_MONITOR); }

bool EvseUiModel::wifiConnected()     { return WiFi.isConnected(); }
bool EvseUiModel::wifiApMode()        { return (WiFi.getMode() & WIFI_MODE_AP) != 0; }
int EvseUiModel::wifiRssi()           { return WiFi.RSSI(); }

const char *EvseUiModel::stateText()
{
  switch (_evse.getEvseState()) {
    case OPENEVSE_STATE_STARTING:           return "Starting";
    case OPENEVSE_STATE_NOT_CONNECTED:      return "Ready";
    case OPENEVSE_STATE_CONNECTED:          return "Connected";
    case OPENEVSE_STATE_CHARGING:           return "Charging";
    case OPENEVSE_STATE_VENT_REQUIRED:      return "Vent required";
    case OPENEVSE_STATE_DIODE_CHECK_FAILED: return "Diode check failed";
    case OPENEVSE_STATE_GFI_FAULT:          return "GFCI fault";
    case OPENEVSE_STATE_NO_EARTH_GROUND:    return "No ground";
    case OPENEVSE_STATE_STUCK_RELAY:        return "Stuck relay";
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED: return "GFCI self-test failed";
    case OPENEVSE_STATE_OVER_TEMPERATURE:   return "Over temperature";
    case OPENEVSE_STATE_OVER_CURRENT:       return "Over current";
    case OPENEVSE_STATE_SLEEPING:           return "Sleeping";
    case OPENEVSE_STATE_DISABLED:           return "Disabled";
    default:                                return "Unknown";
  }
}

#endif // ENABLE_SCREEN_LVGL
```
> The `OPENEVSE_STATE_*` names come from the OpenEVSE lib header (`openevse.h`, pulled via the existing `jeremypoulter/OpenEVSE` dep — `evse_monitor.h` already uses these constants). If any enumerator name differs at compile time, grep the installed `OpenEVSE` lib header for the exact `OPENEVSE_STATE_` names and correct the `case` labels (keep the `default`). Remove any case whose constant doesn't exist rather than inventing one.

- [ ] **Step 3: `src/display_p4/evse_ui_command.h`**
```cpp
#ifndef DISPLAY_P4_EVSE_UI_COMMAND_H
#define DISPLAY_P4_EVSE_UI_COMMAND_H
#if defined(ENABLE_SCREEN_LVGL)

#include <stdint.h>

// User intents from the on-device UI (design spec seam #2). Decouples the LVGL
// screens from ManualOverride / EvseManager claim mechanics.
class IEvseUiCommandSink
{
public:
  virtual ~IEvseUiCommandSink() {}
  virtual void toggleCharge() = 0;                       // start <-> stop
  virtual void setChargeCurrentLimit(uint32_t amps) = 0; // claim manual @ amps
  virtual void clearOverride() = 0;                      // drop manual claim
  virtual bool overrideActive() = 0;                     // is a manual claim held
};

class ManualOverride;

class EvseUiCommandSink : public IEvseUiCommandSink
{
public:
  explicit EvseUiCommandSink(ManualOverride &manual);
  void toggleCharge() override;
  void setChargeCurrentLimit(uint32_t amps) override;
  void clearOverride() override;
  bool overrideActive() override;

private:
  ManualOverride &_manual;
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_EVSE_UI_COMMAND_H
```

- [ ] **Step 4: `src/display_p4/evse_ui_command.cpp`**
```cpp
#if defined(ENABLE_SCREEN_LVGL)

#include "manual.h"
#include "evse_man.h"
#include "evse_ui_command.h"

EvseUiCommandSink::EvseUiCommandSink(ManualOverride &manual) : _manual(manual) {}

void EvseUiCommandSink::toggleCharge()
{
  _manual.toggle();
}

void EvseUiCommandSink::setChargeCurrentLimit(uint32_t amps)
{
  EvseProperties props(EvseState::Active);
  props.setChargeCurrent(amps);
  _manual.claim(props);
}

void EvseUiCommandSink::clearOverride()
{
  _manual.release();
}

bool EvseUiCommandSink::overrideActive()
{
  return _manual.isActive();
}

#endif // ENABLE_SCREEN_LVGL
```
> `EvseProperties(EvseState::Active)` — confirm the ctor takes an `EvseState` (manual.cpp uses `EvseProperties props(EvseState::Active ? ... )`, so a one-arg `EvseState` ctor exists). If not, use the default ctor + `props.setState(EvseState::Active);`.

- [ ] **Step 5: Build** — `pio run -e openevse_p4 2>&1 | tail -25`. Expect SUCCESS (compiles, not yet referenced). Fix any `OPENEVSE_STATE_*` / `EvseProperties` ctor mismatches minimally per the notes above.

- [ ] **Step 6: Commit** — `git add src/display_p4/evse_ui_model.* src/display_p4/evse_ui_command.* && git commit -m "p4 display: add IEvseUiModel + IEvseUiCommandSink seams (over EvseManager/ManualOverride)"`

---

## Task 2: Wire model + command sink into DisplayP4Task; live screen + intent

**Files:** Modify `src/display_p4/display_p4.h`, `src/display_p4/display_p4.cpp`, `src/main.cpp`.

- [ ] **Step 1: `display_p4.h`** — change `begin()` to accept the app objects, and add forward declarations:
  - Add near the top (after `#include <MicroTasks.h>`):
    ```cpp
    class EvseManager;
    class ManualOverride;
    ```
  - Change the public method signature from `void begin();` to:
    ```cpp
    void begin(EvseManager &evse, ManualOverride &manual);
    ```

- [ ] **Step 2: `display_p4.cpp`** — make these edits:
  - Add includes (with the others): `#include "evse_ui_model.h"` and `#include "evse_ui_command.h"`, plus `#include "evse_man.h"` and `#include "manual.h"`.
  - Add file-scope pointers (near the other statics):
    ```cpp
    static IEvseUiModel *s_model = NULL;
    static IEvseUiCommandSink *s_cmd = NULL;
    static lv_obj_t *s_state_label = NULL;   // live state text
    static lv_obj_t *s_values_label = NULL;  // live V / A / kW
    ```
  - Change `void DisplayP4Task::begin()` to:
    ```cpp
    void DisplayP4Task::begin(EvseManager &evse, ManualOverride &manual)
    {
      static EvseUiModel model(evse);
      static EvseUiCommandSink cmd(manual);
      s_model = &model;
      s_cmd = &cmd;
      MicroTask.startTask(this);
    }
    ```
  - Replace the button callback `dp4_btn_event_cb` body so the button issues a real intent (and keep showing it worked):
    ```cpp
    static void dp4_btn_event_cb(lv_event_t *e)
    {
      if (s_cmd) {
        s_cmd->toggleCharge();
      }
      s_touch_count++;
      if (s_touch_count_label) {
        lv_label_set_text_fmt(s_touch_count_label, "Toggles: %u", (unsigned)s_touch_count);
      }
    }
    ```
  - In `dp4_build_bringup_screen()`, replace the static subtitle line with two live labels. Replace the block that creates `sub` with:
    ```cpp
      s_state_label = lv_label_create(scr);
      lv_label_set_text(s_state_label, "State: ...");
      lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
      lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_16, LV_PART_MAIN);
      lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, 64);

      s_values_label = lv_label_create(scr);
      lv_label_set_text(s_values_label, "0.0 V   0.00 A   0.000 kW");
      lv_obj_set_style_text_color(s_values_label, lv_color_hex(0x8A9099), LV_PART_MAIN);
      lv_obj_align(s_values_label, LV_ALIGN_TOP_MID, 0, 90);
    ```
  - Change the bottom label initial text from `"Touches: 0"` to `"Toggles: 0"`.
  - Add a live-refresh helper above `loop()`:
    ```cpp
    static void dp4_refresh_model_view(void)
    {
      if (!s_model) return;
      if (s_state_label) {
        lv_label_set_text_fmt(s_state_label, "State: %s%s", s_model->stateText(),
                              s_model->vehicleConnected() ? "  (EV)" : "");
      }
      if (s_values_label) {
        lv_label_set_text_fmt(s_values_label, "%.1f V   %.2f A   %.3f kW",
                              s_model->voltage(), s_model->amps(), s_model->power() / 1000.0);
      }
    }
    ```
  - In `loop()`, call it on a throttle (the EVSE values change slowly; refresh ~2 Hz). Replace the body of `loop()` with:
    ```cpp
    unsigned long DisplayP4Task::loop(MicroTasks::WakeReason reason)
    {
      lv_timer_handler();

      unsigned long now = millis();
      if (now - _lastModelRefresh >= 500) {
        _lastModelRefresh = now;
        dp4_refresh_model_view();
      }

      if (s_activity) {
        s_activity = false;
        if (!_backlightOn) {
          ledcWrite(DISPLAY_P4_BACKLIGHT_PIN, DP4_BL_FULL);
          _backlightOn = true;
        }
        _backlightDeadline = now + DISPLAY_P4_BACKLIGHT_TIMEOUT_MS;
      } else if (_backlightOn && (long)(now - _backlightDeadline) >= 0) {
        ledcWrite(DISPLAY_P4_BACKLIGHT_PIN, 0);
        _backlightOn = false;
      }

      return 5;
    }
    ```
  - Add the `_lastModelRefresh` member: in `display_p4.h` add `unsigned long _lastModelRefresh;` to the private section, and initialise it to `0` in the `DisplayP4Task::DisplayP4Task()` ctor init list.

- [ ] **Step 3: `src/main.cpp`** — change the guarded call from `displayP4.begin();` to `displayP4.begin(evse, manual);`. (`evse` and `manual` are the existing globals already in scope at that point — `lcd.begin(evse, scheduler, manual);` is on the preceding lines.)

- [ ] **Step 4: Build** — `pio run -e openevse_p4 2>&1 | tail -25`. Expect SUCCESS; report RAM/Flash. Fix minimally.

- [ ] **Step 5: Commit** — `git add src/display_p4/display_p4.h src/display_p4/display_p4.cpp src/main.cpp && git commit -m "p4 display: bind UI model + command sink; live state/values + toggle intent"`

---

## Task 3: On-hardware verification

- [ ] **Step 1: Flash** — `pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5 2>&1 | tail -8`. Expect `[SUCCESS]`.
- [ ] **Step 2: Visual check (user-assisted).** With no controller wired, confirm:
  1. The screen shows a live `State: <text>` line (likely `Starting`/`Ready`/`Unknown` with no controller) and a `0.0 V   0.00 A   0.000 kW` values line that updates (~2 Hz) without flicker/crash.
  2. Tapping the button increments `Toggles: N` and does NOT crash/reboot (it's calling `toggleCharge()` → `ManualOverride::toggle()` against a disconnected EVSE).
  3. Backlight idle timeout still works; display + network still cooperative.
- [ ] **Step 3: Record** D3 outcome in memory esp32-p4-port.md. (Live EVSE values will only be meaningful once a controller is on the RAPI link; the read/command plumbing is what's verified here.)

---

## Self-Review

**Spec coverage (§6):** `IEvseUiModel` (seam #1) ✓, `IEvseUiCommandSink` (seam #2) ✓, both with concrete impls over the existing app objects and now driving the live screen. Panel/flush HAL (seam #3) landed in D2. The three seams of §6 are realized.

**Placeholder scan:** none — full code for all four new files; display_p4 edits are concrete diffs; main.cpp is a one-arg change.

**Type consistency:** `IEvseUiModel`/`EvseUiModel` (model h↔cpp), `IEvseUiCommandSink`/`EvseUiCommandSink` (command h↔cpp); `begin(EvseManager&, ManualOverride&)` matches the new declaration and the main.cpp call; `_lastModelRefresh` declared (h) + initialised (ctor) + used (loop). Model methods map 1:1 to verified `EvseManager` accessors. `EVSE_MONITOR_TEMP_MONITOR` from evse_monitor.h.

**Risk:** `OPENEVSE_STATE_*` enumerator names and the `EvseProperties` ctor arity are the two spots most likely to need a minor name fix at compile; both have explicit fallback instructions inline.
