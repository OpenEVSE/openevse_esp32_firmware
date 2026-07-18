# Character LCD (2×16) Display

This document describes how the ESP32 gateway drives the 2-line × 16-character
text LCD on the OpenEVSE controller, and what is shown in each state.

It applies to the character-LCD builds only. The TFT touchscreen
(`ENABLE_SCREEN_LCD_TFT`) and LVGL (`ENABLE_SCREEN_LVGL_TFT`) variants have
their own UI and are not covered here.

## Ownership model

The character LCD is physically attached to the OpenEVSE ATmega/SAMD
controller, not the ESP32. All display output goes over the RAPI serial link:

| RAPI command | Purpose |
|---|---|
| `$F0 0` | Disable the controller's own display updates (sent once at startup) |
| `$FP <x> <y> <text>` | Write text at column `x`, row `y` (0 = top, 1 = bottom) |
| `$FB <colour>` | Set the backlight colour |

On the first state transition out of `STARTING`, `LcdTask` (`src/lcd.cpp`)
sends `$F0 0` and takes over the front button, so the controller's built-in
messages ("Sleeping", "Connected", …) are only ever visible for a moment at
power-up. From then on the ESP32 paints every character.

Constraints:

- Text is hard-truncated to 16 characters (`LCD_MAX_LEN`); there is no
  scrolling.
- When clearing the remainder of a line, spaces are sent in blocks of at most
  6 per `$FP` command — older controller firmware crashes on longer runs.
- Custom glyphs are available at character codes 1–5: stop, play, lightning,
  lock, clock (`LCD_CHAR_*` in `src/lcd.h`).

## Message API

Modules display transient text through the queue API:

```cpp
lcd.display(msg, x, y, time_ms, flags);
```

- `time_ms` — how long the message holds before the queue advances.
- `LCD_CLEAR_LINE` — blank the rest of the line after the text.
- `LCD_DISPLAY_NOW` — drop any queued messages and show immediately.

When the queue empties, the automatic status screen (below) reasserts itself.
Transient messages come from boot (`OpenEVSE WiFI` + version), network events
(AP SSID/password, hostname, IP address, factory reset), OTA updates
(`Updating WiFi`, percentage, `Complete`/`Error`), and RFID
(tag prompts and results).

## Status screen

The status screen has two independently managed lines:

- **Top line (row 0)** — the state line: the most important fact about the
  current state plus its key number.
- **Bottom line (row 1)** — the info line: rotates through contextual data
  every 4 s (`LCD_DISPLAY_CHANGE_TIME`).

### Top line by state

| State | Display | Notes |
|---|---|---|
| Not connected | `Ready          48A` | Configured charge current |
| Vehicle connected | `Connected      48A` | Configured charge current |
| Charging | `Charging    47.80A` | Live measured amps, 1 s refresh |
| Charging under divert (Eco mode active) | `Eco         16.00A` | Live amps; divert is modulating the rate |
| Sleeping — divert waiting for excess | `Divert      1.23kW` | Live incoming power, 1 s refresh: solar generation (`solar` feed), or grid export (`-grid_ie`) for grid-type divert |
| Sleeping — schedule/timer | `Paused Timer` | Bottom line rotation includes the next `Start` time |
| Sleeping — manual override | `Paused Manual` | Front button or web/API manual stop |
| Sleeping — session limit reached | `Limit Reached` | Energy/time/SOC/range limit |
| Sleeping — remote pause | `Paused Remote` | OCPP, MQTT or Ohm Connect claim |
| Sleeping — other/unknown cause | `zzZ Sleeping Zzz` | E.g. paused directly on the controller |
| Fault | Two fixed lines | e.g. `SAFETY ERROR` / `GROUND FAULT`, `VEHICLE ERROR` / `VENT REQUIRED` |

The pause cause is derived from `EvseManager::getStateClient()` — the
highest-priority claim currently forcing the state (see the client/priority
table in `src/evse_man.h`). The display repaints when the controlling client
changes even if the hardware state does not.

### Bottom line rotation by state

Every entry shows for 4 s, then advances. Entries that need live data
(time, divert power) refresh at 1 s while displayed.

**Not connected / Connected:**
`Energy 1,018Wh` → `Lifetime 2313kWh` → `EVSE Temp 30.5C` →
`Time 3:14:00 PM` → `Date 2020-08-25` →
[`openevse-55ad` → `192.168.1.42`] →
[`Start 10:00 PM`] → [`Stop 6:00 AM`]

**Charging:**
`Energy` → `Lifetime` → `EVSE Temp` →
[if divert active: `Solar 3.41kW` → `Divert 16A`] →
[`openevse-55ad` → `192.168.1.42`] →
[`Stop 6:00 AM`] → [`Left 6:23:00`] →
[if vehicle data: `Charge level 79%` → `Range 259 miles` → `ETA h:mm:ss`]

**Sleeping / Disabled:**
[if paused by divert: `Solar 3.41kW` → `Avail 4.2A`] →
`Time` → `Date` →
[`openevse-55ad` → `192.168.1.42`] →
[`Start 10:00 PM`] → [`Stop 6:00 AM`]

The hostname and IP address entries appear in every non-fault state and are
controlled by the `lcd_network_info` config setting (boolean, default
enabled; persisted in the config store). Disable via the config API/MQTT
(`{"lcd_network_info": false}`) to remove them from the rotation. Fault
states show fixed two-line error text and never rotate.

Divert-related entries:

| Entry | Meaning |
|---|---|
| `Solar 3.41kW` | Solar generation feed (solar-type divert) |
| `Grid IE -500W` | Grid import/export feed, negative = exporting (grid-type divert) |
| `Divert 16A` | Charge rate divert has set (while charging) |
| `Avail 4.2A` | Smoothed current available for divert — charging starts when it exceeds the minimum charge current (typically 6 A) |

A manual override forces the bottom line to `Manual Override` until released.

### Backlight colour

Set from `EvseManager::getStateColour()` on every state/flag change:

| State | Colour |
|---|---|
| Not connected | Green |
| Connected | Yellow |
| Charging | Teal |
| Sleeping/Disabled | Teal if vehicle connected, else violet |
| Any fault | Red |
