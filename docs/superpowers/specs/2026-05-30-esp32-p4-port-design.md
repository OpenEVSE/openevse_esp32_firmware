# ESP32-P4 Port — Design

**Date:** 2026-05-30
**Status:** Design / awaiting review
**Author:** brainstormed with Andrew Rankin
**Target hardware:** Guition **JC4880P443C_I_W** (ESP32-P4 + ESP32-C6 + 4.3" ST7701S)

---

## 1. Goal

Port the OpenEVSE ESP32 WiFi firmware to the Guition **JC4880P443C_I_W** module as a
**production, shippable** target, and do so by **migrating the whole codebase to
Arduino-ESP32 3.x first**, then adding the new board on that baseline so the project
keeps a single maintainable tree.

The on-device UI on the P4 will be **LVGL** with **touch**.

This is a full port plan, not a feasibility spike. It will be turned into an
implementation plan (`writing-plans`) after review.

---

## 2. Target hardware

| Aspect | Value | Source |
|---|---|---|
| App SoC | ESP32-P4, dual RISC-V @ 360 MHz, 768 KB HP L2MEM, 32 KB LP SRAM | Spec PDF |
| Radio | ESP32-C6 over **ESP-Hosted (SDIO)**, slave firmware pre-flashed | esphome yaml, spec |
| PSRAM | 32 MB | Spec PDF |
| Flash | 16 MB (QIO, 80 MHz) | esphome yaml |
| Panel | **ST7701S**, **480×800**, MIPI-DSI 2-lane, 24-bit RGB, IPS | Spec PDF, vendor drivers |
| Touch | **GT911** capacitive, I²C | esphome yaml |
| Module | `JC-ESP32P4-M3` (P4 + C6 core board) | Spec PDF |

### Confirmed pin map (from vendor `esphome-jc4880p443c.yaml` + examples)

| Function | Pins |
|---|---|
| C6 ESP-Hosted (SDIO slot 1) | CLK=18, CMD=19, D0=14, D1=15, D2=16, D3=17, RESET=54, active-high |
| Touch + codec I²C (`bus_main`) | SDA=7, SCL=8, 400 kHz; GT911 INT=21, RST=22 |
| Display backlight | GPIO23 (LEDC PWM) |
| MIPI-DSI PHY power | LDO channel 3 @ 2.5 V |
| Status LED | GPIO26 |
| Boot button | GPIO35 (input pull-up, inverted) |
| (unused) Audio ES8311 I²S | LRCLK=10, BCLK=12, MCLK=13, DOUT=9, DIN=48, PA-EN=11 |
| (unused) SD/TF (SDMMC slot 0) | CLK=43, CMD=44, D0=39 |

### Exposed connectors for OpenEVSE peripherals (Spec PDF "Interface Description")

- **MX1.25 4-pin UART0** and a **second MX1.25 4-pin UART** — RAPI serial link to the
  OpenEVSE controller lands on one of these.
- **MX1.25 4-pin RS-485** — available, not required.
- **HS 1.0 2-pin I²C** connector — for PN532 / MCP9808 (separate from the touch bus).
- **2×13 (26-pin) 2.54 mm GPIO header** — WS2812 data line + WiFi button + spare GPIO.

Exact header pin numbers are confirmed against `5-Schematic/JC4880P443_V1.0.pdf` during
implementation (Phase P3).

---

## 3. Scope

**In scope**
- Project-wide Arduino-ESP32 **2.0.x → 3.x** migration (PlatformIO platform + dependencies).
- New `[env:openevse_p4]` build target.
- WiFi/BLE via the C6 (ESP-Hosted); STA + SoftAP + captive portal + mDNS + TLS.
- OpenEVSE peripherals: RAPI serial link, PN532 RFID, MCP9808 temperature, WS2812 LEDs.
- LVGL display subsystem on MIPI-DSI, **with touch (GT911)**, portrait 480×800.
- Dual-target OTA (P4 application **and** C6 radio firmware).
- New 16 MB partition table.

**Out of scope**
- Board peripherals irrelevant to an EVSE: camera (OV02C10), audio codec (ES8311),
  SD/TF storage, lithium-battery gauge.
- The legacy-hardware LVGL spike (separate effort, separate checkout/branch).
- The decision on whether the P4 UI *shares code* with the legacy LVGL UI — deferred by
  the user. The P4 UI is designed to make sharing **possible** but does not depend on it.

---

## 4. Strategy: migrate the whole tree to core 3.x first

The ESP32-P4 is only supported by Arduino-ESP32 **3.x** (ESP-IDF 5.3+). The chosen
approach (over isolating P4 or forking) is to move the entire project to the 3.x baseline
so there is one codebase.

1. **Platform move.** Replace `platform = espressif32@6.12.0` with the **pioarduino** fork
   (`pioarduino/platform-espressif32`, the 5x.x line = core 3.x / IDF 5.3). Official
   `platformio/espressif32` did not ship clean 3.x / P4 support; pioarduino is the
   practical route. **Every** existing `[env:]` moves to this platform.
2. **Dependency fixes.** The pinned libraries must compile against core 3.x / IDF 5:
   `ArduinoMongoose`, `OpenEVSE`, `ESPAL`, `MicroTasks`, `ConfigJson`, `StreamSpy`,
   `MicroOcpp` + `MicroOcppMongoose`. Highest-risk: `ArduinoMongoose`'s mbedTLS glue
   (IDF 5 ships **mbedTLS 3.x**, which made many previously-public structs opaque) and
   any `WiFi`/`Stream`/`Serial` API drift.
3. **Retest existing boards.** The WROOM (`openevse_wifi_v1`), Olimex gateway/PoE, WT32,
   Heltec, Adafruit, C3 envs must still build and boot on the new baseline. This is the
   cost of a single clean tree.
4. **Add the P4 env** on the migrated baseline.

### P0 feasibility probe (first step, recommended)

Before the full migration, do one cheap probe: get the **current** web-server + Mongoose +
mbedTLS stack **compiling** on pioarduino core 3.x (existing WROOM env, no P4 yet). The TLS
glue is the likeliest thing to break the whole plan; learning that on day one is far
cheaper than discovering it after porting four boards. If it compiles, confidence in the
whole migration jumps; if it explodes, we re-plan the TLS layer before investing further.

---

## 5. Subsystem approaches

### 5.1 Networking (C6 / ESP-Hosted)
`net_manager.cpp` ports largely **as-is**: standard `WiFi.h` calls are transparently
serviced by the C6 over ESP-Hosted (proven by the vendor `Wifi_scan` example, which uses
plain `WiFi.mode()` / `WiFi.scanNetworks()` with no bridge code). New work:
- Configure ESP-Hosted SDIO for **this board's** pins (CLK18/CMD19/D0–3=14–17/RST54/slot1)
  — via board variant / build flags / sdkconfig for the P4 env.
- Validate the less-common paths over the remote radio: **SoftAP + captive-portal DNS**,
  **mDNS**, and **mbedTLS/TLS** (`WiFiClientSecure`-equivalent through Mongoose).
- Treat the **C6 slave firmware as a managed OTA target** (see 5.4).
- `esp_wifi_set_country_code()` (currently called directly) — confirm the equivalent under
  `esp_wifi_remote` on IDF 5.

### 5.2 Display subsystem (LVGL on MIPI-DSI, with touch)
New subsystem, replacing the TFT_eSPI path on this board:
- `esp_lcd` **ST7701S MIPI-DSI** bring-up (vendor driver `esp_lcd_st7701_mipi.c` exists);
  enable the **DSI PHY LDO** (channel 3, 2.5 V); allocate the **framebuffer in PSRAM**.
- **LVGL 8.x** (pinned to the same minor as the legacy effort), flush callback bound to the
  DSI panel; `lv_timer_handler` pumped from a `MicroTasks::Task` so it cooperates with the
  network/web tasks rather than starving them.
- **GT911 touch** driver → LVGL input device (`indev`); interactive UI.
- Backlight on **LEDC PWM (GPIO23)** with the existing idle-timeout behaviour.
- Screens (boot / charge / lock / fault) rebuilt as LVGL for **480×800 portrait** (the
  current UI is 480×320 landscape, so this is a relayout, not a pixel port). They read
  from `IEvseUiModel` and emit user intents to the command sink (see §6).

### 5.3 Peripherals & pins
- **RAPI serial** → an `HardwareSerial` on one of the board's MX1.25 UART connectors.
- **PN532 RFID** + **MCP9808 temp** → the dedicated **I²C connector**, kept on a separate
  bus from the GT911 touch bus to avoid contention.
- **WS2812** LEDs → a free GPIO on the 2×13 header; `LedManagerTask` is otherwise portable.
- **WiFi button** → a free GPIO (or reuse the boot button, GPIO35).

### 5.4 OTA & partitions
- New **16 MB partition table** for the P4 (dual app/OTA + SPIFFS + coredump), replacing the
  ESP32-specific CSVs.
- The existing `/update` HTTP flow (`Update.*`) covers the **P4 application** image.
- **Add a C6 firmware update path** (ESP-Hosted slave OTA, driven from the P4) so a field
  update can refresh both chips. This is new and production-relevant — a shipped unit must
  be able to update its radio.

---

## 6. The seam with the LVGL effort

The P4 UI is LVGL and touch-interactive. To let it compose with (and potentially later
share code with) the separate LVGL effort, three interfaces are agreed up front:

1. **`IEvseUiModel` (data → UI).** Read-only view of: charge state, power / current /
   voltage / energy, temperature, network + Wi-Fi status, lock / fault state, RFID prompts.
   Replaces today's direct reads of `EvseManager`/`EvseMonitor` from the screen classes.
2. **Command / event sink (UI → app).** User intents from touch: start / stop / override
   charge, adjust current limit, confirm / dismiss prompts (incl. RFID), navigate screens.
3. **Panel / flush HAL (UI → hardware).** A MIPI-DSI backend for the P4; the legacy SPI
   backend stays where it is. The same LVGL UI can bind to either.

Plus a cross-cutting agreement: **pin LVGL to a shared 8.x minor** with the legacy effort.

---

## 7. Phased plan

| Phase | Work | Exit criterion | Risk |
|---|---|---|---|
| **P0a** | Feasibility probe: current web/TLS/Mongoose stack compiles on pioarduino core 3.x (WROOM env) | `pio run` succeeds; TLS path links | Highest |
| **P0b** | Full core-3.x migration + dependency fixes; all existing envs build & boot | Existing boards green | Highest |
| **P1** | P4 env: boots, serial, PSRAM, partitions | P4 boots to app, heap/PSRAM sane | Low |
| **P2** | C6 networking: STA + SoftAP + captive portal + mDNS + TLS + web server | Web UI reachable, HTTPS works | Medium |
| **P3** | Peripherals: RAPI link, RFID, temp, WS2812, button | EVSE talks to controller; sensors read | Low–Med |
| **P4** | LVGL display + touch + `IEvseUiModel` + command sink + portrait screens | Interactive UI shows live EVSE data | Medium |
| **P5** | Dual-target OTA (P4 app + C6 fw), partition finalization | Both chips field-updatable | Medium |
| **P6** | Integration, soak test, resolve UI code-sharing decision | Stable on bench | — |

---

## 8. Top risks

1. **mbedTLS 3.x vs ArduinoMongoose** — IDF 5's mbedTLS 3.x opaque-struct changes are the
   sharpest edge of the migration; may require patching the TLS glue. *Mitigated by P0a.*
2. **Dependency compatibility on core 3.x** — the jeremypoulter libraries and MicroOcpp are
   not guaranteed to build on IDF 5; some may need forking/patching.
3. **C6 ESP-Hosted edge cases** — SoftAP / captive portal / TLS over the remote radio are
   less-travelled than plain STA scanning.
4. **Migrating every existing board** — broadest surface, lowest novelty; mostly a retest
   slog rather than a design problem.
5. **DSI bring-up specifics** — LDO sequencing, framebuffer sizing in PSRAM, LVGL flush
   timing; the vendor driver de-risks this substantially.

---

## 9. Open questions (carried into implementation)

- Exact GPIO header pin numbers for RAPI UART / WS2812 / button — confirm against the
  schematic (P3).
- Whether the C6 slave firmware shipped on the board matches the ESP-Hosted version the
  core 3.x toolchain expects, or must be reflashed/managed.
- Final partition sizing (SPIFFS/web assets vs OTA slots) within 16 MB.
- The legacy-vs-P4 UI code-sharing decision (deferred; revisited at P6).
