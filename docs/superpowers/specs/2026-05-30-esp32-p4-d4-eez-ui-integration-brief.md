# P4 Display D4 — EEZ Studio UI Design & Integration Brief

This is the contract between the **EEZ Studio design** (your part) and the **firmware integration** (my part). If the screens/widgets are named and configured as below, your exported LVGL code drops into the firmware with minimal wiring: the existing `IEvseUiModel` feeds the widgets and the `IEvseUiCommandSink` receives the button taps.

Status: scaffold built ahead of the export — `P4ScreenManager` (state machine) + a gated `ENABLE_EEZ_UI` hook are in place. The current D3 bring-up screen stays active until the export + flag land.

---

## 1. EEZ Studio project settings (important for compatibility)

| Setting | Value | Why |
|---|---|---|
| Project type | **LVGL** | (not "EEZ Flow Dashboard") |
| **LVGL version** | **8.x** (8.3 or 8.4) | Firmware pins `lvgl@^8.3.9` (resolves to 8.4.0). A 9.x export is a major reconciliation — avoid. |
| Display width × height | **800 × 480** (landscape) | The firmware renders landscape via LVGL software rotation (the ST7701/DSI panel is physically 480×800 portrait but is rotated 90°). Design your screens at **800 wide × 480 tall**. |
| Color depth | **16-bit (RGB565)** | Matches `lv_conf.h` (`LV_COLOR_DEPTH 16`, no swap). |
| Flow | **Disabled ("without Flow")** *(recommended)* | Exports plain LVGL (`ui_init()` / `ui_tick()` + an `objects` struct). I bind data from firmware. "With Flow" works too but pulls in the eez-flow runtime lib and you'd bind data inside EEZ via native variables — tell me if you go that way and I'll adapt. |
| Fonts | Montserrat (any sizes you like) | EEZ bundles fonts into the export, so no `lv_conf` font coordination needed. |

When you **Build** in EEZ Studio it generates a `ui/` folder (typically: `ui.c/.h`, `screens.c/.h`, `styles.c/.h`, `images.c/.h`, `fonts.c/.h`, and `actions.c/.h`). Hand me that whole folder; I'll place it at **`src/display_p4/ui/`**.

---

## 2. Screens to create (exact names)

Create four screens with these exact names (EEZ "Page" names → become `objects.<name>` / screen-load IDs):

| EEZ screen name | Shown when (EVSE state) |
|---|---|
| `screen_boot` | startup, until the first valid EVSE state |
| `screen_charge` | connected / charging / active (the main screen) |
| `screen_sleeping` | disabled / sleeping (EV not charging, EVSE idle) |
| `screen_fault` | any error/fault state |

The firmware's `P4ScreenManager` decides which one is active from `IEvseUiModel` and loads it; you just design them.

---

## 3. Widgets to place (exact names) + what I bind to each

Name the widgets exactly as below (EEZ "Name" field on each widget). Anything not listed is yours to style freely. Values in parentheses are the `IEvseUiModel` source I'll push in each tick (~2 Hz).

### `screen_charge` (the main one)
| Widget name | Type | Bound to |
|---|---|---|
| `charge_state_label` | Label | `stateText()` (e.g. "Charging", "Ready") |
| `charge_kw_value` | Label | `power()/1000` → `"%.2f"` (big kW number) |
| `charge_amps_value` | Label | `amps()` → `"%.1f A"` |
| `charge_volts_value` | Label | `voltage()` → `"%.1f V"` |
| `charge_energy_value` | Label | `sessionEnergy()` → scaled Wh/kWh |
| `charge_elapsed_value` | Label | `sessionElapsed()` → `"H:MM:SS"` |
| `charge_temp_value` | Label | `temperatureC()` (hide/`--` if `!tempValid()`) |
| `charge_power_ring` | Arc | `power()` scaled to the arc range (0–100) |
| `charge_wifi_label` | Label | wifi status from `wifiConnected()`/`wifiApMode()`/`wifiRssi()` |
| `btn_start_stop` | Button | **CLICKED → `commandSink.toggleCharge()`** |
| `btn_start_stop_label` | Label (on button) | I set "START"/"STOP" from `active()` |

(You don't have to use every widget — omit any you don't want and I'll skip its binding. The arc range/rotation/styling is entirely yours.)

### `screen_boot`
| Widget name | Type | Bound to |
|---|---|---|
| `boot_status_label` | Label | firmware sets a short status string |
| `boot_progress` | Bar or Arc *(optional)* | boot progress (I drive it) |

### `screen_sleeping`
| Widget name | Type | Bound to |
|---|---|---|
| `sleeping_state_label` | Label | `stateText()` |
| `btn_wake` *(optional)* | Button | CLICKED → `commandSink.toggleCharge()` |

### `screen_fault`
| Widget name | Type | Bound to |
|---|---|---|
| `fault_text_label` | Label | `stateText()` (the fault description) |

---

## 4. Events you can wire in EEZ (optional)

If you stay non-Flow, leave events unwired — I attach them in firmware by widget name (`btn_start_stop` → `toggleCharge`). If you'd rather wire them in EEZ as **Actions**, name the action `action_toggle_charge` and I'll implement it in `actions.c`'s firmware-side hook.

---

## 5. How it plugs in (my side — for reference)

1. Drop the EEZ `ui/` folder at `src/display_p4/ui/`; add it to the build.
2. Add `-D ENABLE_EEZ_UI` to `[env:openevse_p4]` (flips the display task from the D3 bring-up screen to the EEZ UI).
3. In `DisplayP4Task::setup()`: call `ui_init()` instead of building the bring-up screen.
4. In `DisplayP4Task::loop()`: call `ui_tick()`, then push `IEvseUiModel` values into `objects.<name>` and attach the `btn_start_stop` event to `toggleCharge()` (done once after init).
5. `P4ScreenManager` picks the active screen from EVSE state and calls EEZ's screen-load.

Name mismatches between your export and §3 are a quick fix on my side, not a redesign — so don't sweat exact spelling; close is fine.

---

## 6. Quick checklist before you export
- [ ] LVGL version = 8.x
- [ ] 800 × 480 (landscape), 16-bit color
- [ ] Four screens named `screen_boot` / `screen_charge` / `screen_sleeping` / `screen_fault`
- [ ] `screen_charge` has at least `charge_state_label`, `charge_kw_value`, `btn_start_stop` (+`btn_start_stop_label`)
- [ ] Build → hand me the generated `ui/` folder
