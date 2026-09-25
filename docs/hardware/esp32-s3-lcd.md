# OpenEVSE ESP32-S3 LCD board

Firmware notes for the ESP32-S3 WiFi/LCD board (`env:openevse_s3_lcd`).

Extracted from the schematics, not from §2 of the design doc — **§2 is stale**, it
still describes v1.1's `JP6` and `PILOTREAD`, neither of which exists.

**Two versions are live, and they differ.**

| | |
|---|---|
| **v1.2.0** | The boards that were **actually fabricated**. This is the firmware target. |
| **v1.3.0** | Designed, routed and verified, **never ordered**. Adds two GPIOs, four external pull-ups and fixes the backlight. |

Everything below is v1.2 unless marked **v1.3**. One firmware image is intended to
run on both.

## Module

**ESP32-S3-WROOM-1U-N16R8** — 16 MB quad SPI flash, 8 MB **octal** PSRAM, U.FL
external antenna (no PCB antenna).

- Octal PSRAM consumes **IO35, IO36, IO37** — unavailable.
- GPIO22–34 are not bonded out on WROOM-1 at all.
- Build config: `board_build.arduino.memory_type = qio_opi`, 16 MB flash,
  `-D BOARD_HAS_PSRAM`.

### What the 8 MB of PSRAM is actually doing

**Less than the sdkconfig suggests.** Arduino-ESP32 ships prebuilt IDF libraries, so
`qio_opi/include/sdkconfig.h` is fixed and not editable from the app side. Measured
on the first board (`psram_free` in `/status`): the SDK on its own puts about 10 KB
there. What is set and what is not:

```
CONFIG_SPIRAM_USE_MALLOC              1     PSRAM is in the general heap
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL   4096  malloc() >= 4 KB goes to PSRAM
CONFIG_SPIRAM_MODE_OCT / SPEED_80M
CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC     1     mbedTLS is pinned to internal DRAM
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP  NOT SET  WiFi/lwIP pools stay internal
```

So the 4 KB rule is the only thing that happens by default, and most of the firmware's
allocations are smaller than that. The firmware moves two things itself:

- **mbedTLS** — `psram_setup()` in `main.cpp` installs a PSRAM-preferring allocator
  through `mbedtls_platform_set_calloc_free()` before the network comes up (the port
  is built with `MBEDTLS_PLATFORM_MEMORY`, so this is a supported runtime swap).
  SSL contexts, the 16 KB/4 KB record buffers and parsed certificate chains land in
  PSRAM; on the stock board those are the largest contiguous internal allocations.
- **The LVGL object pool** — `lv_conf.h` takes the pool from PSRAM via
  `LV_MEM_POOL_ALLOC` on `BOARD_HAS_PSRAM` and grows it to 64 KB.

**The env now builds Arduino as an ESP-IDF component** (`framework = arduino, espidf`,
same as `openevse_wifi_tft_v1`), so the Kconfig is ours. `sdkconfig.defaults.esp32s3`
is the S3 seed (copied from the core's `esp32s3/sdkconfig`), `sdkconfig.defaults.s3lcd`
is this board's overlay: octal PSRAM, qio flash, `SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`
(WiFi/lwIP pools in PSRAM), `MBEDTLS_EXTERNAL_MEM_ALLOC=y` (which replaces the runtime
hook above; `psram_setup()` compiles out), and `BT_ENABLED=n`. Every knob must be
checked in the generated `sdkconfig.openevse_s3_lcd*` after a build -- Kconfig drops
unknown symbols silently (a `DYNAMIC_TX_BUFFER_NUM` line was, because this core uses
static TX buffers). Cold build ~3 min instead of ~70 s.

Two consequences worth carrying:

- **Anything that must be internal has to say so.** The LVGL draw buffer already
  does — `heap_caps_malloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` — because a
  DMA-capable or latency-sensitive buffer has no business in external RAM.
- **`heap_largest` / `heap_largest_min` in `/status` are internal-only** by design;
  `psram_free` / `psram_largest` show the external side.

## Pin map

| GPIO | Net | Function | Firmware notes |
|---|---|---|---|
| 0 | `IO0_BOOT` | BOOT button → GND | strapping; also JP5.2; `WIFI_BUTTON` |
| 1 | `EVSE_TX` | UART1 TX → JP1.5 | 220 Ω series (R37); `TX1` |
| 2 | `EVSE_RX` | UART1 RX ← JP1.4 | `RX1`; **pull-up needed on v1.2** |
| 8 | `SDA` | I²C0 SDA | 4.7 kΩ pull-up on board |
| 9 | `SCL` | I²C0 SCL | 4.7 kΩ pull-up on board |
| 10 | `TFT_CS` | ILI9488 CSX | FSPI IO_MUX |
| 11 | `TFT_MOSI` | ILI9488 SDA | FSPI IO_MUX |
| 12 | `TFT_SCLK` | ILI9488 SCL | FSPI IO_MUX |
| 14 | `TFT_DC` | ILI9488 D/CX (RS) | |
| 15 | `BL_PWM` | backlight gate | **active HIGH**, 10 kΩ pull-down |
| **16** | `PWR_ST` | **v1.3** — TPS2116 status | **high = on USB, low = on EVSE 5 V** |
| **17** | `RTC_INT` | **v1.3** — DS3231 alarm | open-drain, **active low**, 10 kΩ pull-up |
| 18 | `LED_DIN` | WS2812B chain in | RMT |
| 19 | `USB_DM` | native USB D− | USB-Serial-JTAG |
| 20 | `USB_DP` | native USB D+ | USB-Serial-JTAG |
| 21 | `TFT_RST` | ILI9488 RESET | **active low** |
| 38 | `SD_CLK` | microSD CLK | |
| 39 | `SD_CMD` | microSD CMD | 10 kΩ pull-up |
| 40 | `SD_D0` | microSD DAT0 | 10 kΩ pull-up |
| 41 | `SD_CD` | card-detect switch | **active low, pull-up needed on v1.2** |
| 42 | `TEMP_ALERT` | MCP9808 ALERT | open-drain, 10 kΩ pull-up, **active low** |
| 43 | `S3_TXD0` | UART0 TX → JP5.5 | ROM/bootloader console; `Serial0` in app |
| 44 | `S3_RXD0` | UART0 RX ← JP5.4 | ROM/bootloader console; `Serial0` in app |
| 47 | `AUX_TX` | UART2 TX → JP2.5 | 220 Ω series (R38) |
| 48 | `AUX_RX` | UART2 RX ← JP2.4 | **pull-up needed on v1.2** |
| **45** | `RAPI_TX_LED` | **v1.6.1** — LED6, red | **active HIGH**, 1 kΩ to GND; strapping (VDD_SPI) |
| **46** | `RAPI_RX_LED` | **v1.6.1** — LED7, yellow | **active HIGH**, 1 kΩ to GND; strapping (boot log) |
| EN | `RESET` | RESET button | 10 kΩ + 1 µF, also JP5.6 |

**Unrouted / unavailable:** IO3, IO4, IO5, IO6, IO7 and IO13 exist on the module
castellations but connect to nothing — bodge wire only. **IO45 and IO46 were in that
list until v1.6.1**, which spends them on the two RAPI activity LEDs below. IO35–37 are consumed
by PSRAM. **IO16 and IO17 are also unrouted on v1.2**; they are spent in v1.3.

There is **no** `PILOTREAD` and **no ADC input of any kind** on either version. Pilot
state arrives over the RAPI link, not by measurement.

## Pull-ups — the difference that will bite you

**On v1.2 the internal pull-up is load-bearing on IO2, IO48 and IO41.** They read as
dead or stuck-low otherwise.

1. **IO2 (`EVSE_RX`)** and **IO48 (`AUX_RX`)** — level shifting is a bare 1N4148
   (D3, D2) with its **anode on the GPIO** and cathode on the 5 V side. The external
   driver pulls the node down through the diode; nothing pulls it back up. The low
   level sits at **~0.7 V, not 0 V** — comfortably under V<sub>IL</sub>, but not a
   rail-to-rail signal.
2. **IO41 (`SD_CD`)** — the detect switch shorts to GND on insertion. No external
   pull-up (R33's is on DAT3, a different net).

**v1.3** fits 10 kΩ externally on all three plus IO0 (`R39`–`R42`), so the internal
ones stop being load-bearing. Enabling them anyway is harmless and keeps one firmware
image working on both.

Firmware applies this for `EVSE_RX` via `RAPI_RX_PULLUP` (see `src/debug.cpp`).
It uses `gpio_set_pull_mode()`, **not `pinMode()`** — on Arduino-ESP32 3.x, `pinMode()`
runs the peripheral manager and would detach the UART from the pin it was just
configured on. `AUX_RX` and `SD_CD` are not handled yet only because nothing opens
UART2 and there is no SD driver — `SD_CD`'s pull-up becomes load-bearing the moment
one lands.

## I²C bus (IO8 SDA / IO9 SCL)

| Address | Device | Notes |
|---|---|---|
| `0x18` | MCP9808 temperature | A0/A1/A2 all tied GND; `ENABLE_MCP9808` |
| `0x68` | DS3231MZ RTC | CR2032 backup on BT1; `ENABLE_DS3231`, `src/rtc_ds3231.*` |
| — | Qwiic connector | external, 4-pin JST-SH |

The MCP9808's `ALERT` is wired to IO42 on both versions; the firmware polls over I²C
and does not use it.

The DS3231's `32KHZ` and `RST` are **not connected** on either version. Its `INT/SQW`
is **not connected on v1.2 — poll it**; on **v1.3** it reaches IO17, so alarms and
RTC-driven wake become available. The driver polls and uses no alarms, so it works
unchanged on both.

### Why the RTC is not optional

`tsdb_energy_logger` **discards every sample** until wall-clock passes
`TSDB_TIME_VALID_FLOOR` (2023-11-14), because writing a pre-NTP epoch would corrupt
the time index. This board is powered by the EVSE, so it loses power whenever the
EVSE does — and the case that costs the most data is the one where WiFi is also down
and NTP never answers. The coin cell (~225 mAh against the DS3231MZ's ~2.5 µA) is
what closes that hole.

- **Boot:** `rtc_begin()` + `rtc_seed_system_time()` run in `setup()` *before*
  `timeManager.begin()` and well before the logger starts, so `time(NULL)` is already
  past the floor and the logger needs no special case.
- **On sync:** `TimeManager::setTime()` writes back to the RTC (SNTP, `/time`, the
  login-supplied time), and so does `input.cpp`'s `handleRapiRead()` when it adopts
  the controller's clock. The controller is the weaker source — it is often the one
  *we* set — but it is also the only one available in exactly the case the cell
  exists for: WiFi down and NTP never answering. The floor check below is what keeps
  an unset controller from laundering a 1970 into the cell.
- **A dead or missing cell presents as "no time", never as 1970 or 2000-01-01.** The
  oscillator-stop flag is checked first, and the decoder rejects anything below the
  same floor. `RTC_TIME_VALID_FLOOR` and `TSDB_TIME_VALID_FLOOR` are held equal by a
  `static_assert`.

The register codec is pure and host-tested (`test/test_rtc_ds3231/`, run under
`native_test`) — it covers BCD validation, the 12-hour-mode read path, leap days,
the century bit, and non-existent dates such as 31 February. None of it is
hardware-validated yet.

## Power — v1.2 is blind, v1.3 is not

VBUS from USB-C and VBUS from the EVSE controller are merged by a TPS2116 priority
mux, then an AP63201 buck makes 3.3 V.

**On v1.2 no GPIO senses any of it** — firmware cannot tell USB from EVSE power, and
there is no coin-cell monitor.

**v1.3 wires the TPS2116's `ST` pin to IO16.** Per SLVSFG1A Table 5-1 it is an *"open
drain status pin, pulled low when VIN1 is not being used"*, and VIN1 is USB. So:

- **IO16 high** → running on **USB**
- **IO16 low** → running on the controller's **5 V**

10 kΩ pull-up (R43), 5 µs response. There is still no coin-cell monitor.

## Display — ILI9488, 320×480, 3.5"

**BuyDisplay ER-TFT035-6.** `IM[2:0]` all pulled to 3.3 V through 1 kΩ (R3/R6/R8) →
`111` = **4-wire 8-bit serial (SPI)**, per datasheet §4.1 Note 1. Identical on both
versions. Driven by the same LVGL renderer as the stock TFT
(`ENABLE_SCREEN_LVGL_TFT`, `src/lvgl_tft/`), over TFT_eSPI.

- **There is no real MISO, but `TFT_MISO` must not be `-1`.** In the strapped mode
  the datasheet's own table reads `1 1 1 | 4-wire 8-bit data Serial Interface I |
  SCL, SDA/SDO, D/CX, CSX` — `SDA/SDO` is **one combined pin**, and pin 34 `SDA` is
  *"serial input or serial data input/output bi-direction."* **Readback returns on
  SDA, not SDO**, so wiring the (open) `SDO` would have bought nothing.

  Found on the first board: TFT_eSPI 2.5.43 aliases `TFT_MISO=-1` to `TFT_MOSI` on
  the S3 (not just the C3/S2), so `SPIClass::begin(12, 11, 11, -1)` attaches IO11 as
  MISO and then as MOSI. Core 3.x's peripheral manager runs the SPI deinit on that
  second attach, which stops the bus, and the first `writecommand()` then spins on
  `cmd.usr` until the task watchdog fires. The build therefore names **IO13** — the
  FSPI `Q` IO_MUX pin, unconnected on both revisions — as a dummy MISO. Nothing
  reads it.

  Also found on the first board: core 3.x defines `FSPI` as 0 on the S3 and IDF's
  `REG_SPI_BASE(0)` is 0, so TFT_eSPI's default `SPI_PORT = FSPI` aimed every raw
  register write at address `0x10` (StoreProhibited on the first command).
  `USE_FSPI_PORT` gives it `SPI_PORT = 2`, the real GPSPI2 base, while it still
  opens `SPIClass(FSPI)` on the same bus.

  A bring-up ID check is therefore *possible*, just not through TFT_eSPI or
  `esp_lcd`'s panel-IO layer: it is a half-duplex read on IO11 (`SPI_DEVICE_3WIRE`
  drives MOSI bidirectionally), issued as a raw `spi_master` transaction **before**
  the panel driver takes the bus. Worth having on hardware that has never been
  powered on. Not implemented yet.
- **No tearing sync** — `TE` is not connected.
- **No touch** — the panel is behind a sealed cover; `XL`/`XR`/`YU`/`YD` are N/C, so
  `TOUCH_CS` is deliberately left undefined.
- All 18 parallel data pins plus `DE`, `PCLK`, `HSYNC`, `VSYNC` are tied to GND, as
  the datasheet requires for unused-bus operation.
- `RESET` on IO21 is independent of MCU reset — the panel can be re-inited without
  rebooting. (This was a v1.0.2 defect; don't assume the old behaviour.)
- IO10/11/12 are the FSPI **IO_MUX** pins, so the bus bypasses the GPIO matrix and
  can clock high — that was a deliberate board decision, not an accident. TFT_eSPI's
  default port on the S3 is `SPI` = FSPI = SPI2, which is what these pins belong to,
  so no `USE_HSPI_PORT`.
- **The renderer is shared with the shipped stock TFT board.** `openevse_wifi_tft_v1`
  and `openevse_s3_lcd` both pull in `${common.lvgl_tft_renderer_flags}`; there is no
  separate S3 panel layer. Any rework of `src/lvgl_tft/lvgl_panel.cpp` lands on
  hardware in the field unless it is conditioned on the board. Prove it on the S3
  first — the stock board has no PSRAM to stage a second buffer in, and if a change
  turns out marginal there, there is nothing to recall.
- **The build ships 40 MHz and 80 MHz needs measuring on the first board.** The wire
  format is 18 bpp (see below), so a full 15360-pixel draw buffer is 46 KB; at 40 MHz
  (5 MB/s) that is **~9.2 ms of blocked CPU per flush**, ~92 ms for a full repaint.
  80 MHz halves both. If it turns out marginal, that is the argument for 33 Ω series
  termination on `TFT_SCLK`/`TFT_MOSI` in v1.3 — **there is none anywhere on the TFT
  bus today**, and `TFT_MOSI` is 57.7 mm of board copper plus ~30 mm of flex tail
  against a ~75 mm critical length. v1.3 is designed but not ordered, so that window
  is open now and shuts at fab.

  **Do not backport a clock bump to the stock board on the strength of an S3 result.**
  80 MHz is available there in principle — its `TFT_MOSI=13` / `TFT_SCLK=14` /
  `TFT_CS=15` are the ESP32-classic HSPI IO_MUX pins, so it bypasses the GPIO matrix
  the same way — but that board is the QD354801 direct-solder 48-land part and this
  one is an ER-TFT035-6 on FPC through a ZIF. Same controller, different trace lengths
  and flex path. A DMA rework is a software risk that can be settled on a bench; a
  clock bump on shipped hardware is a signal-integrity bet that cannot be taken back.

### ⚠ Backlight: broken on v1.2, fixed on v1.3

`BL_PWM` on IO15 drives an AO3400A N-channel low-side switch — **active high**, 10 kΩ
pull-down, so the backlight is **off at reset**. Six LED cathodes each return through
a 120 Ω ballast.

**On v1.2, `J1.LEDA` is on the 3.3 V rail.** The design doc's fix (§12 item 8) calls
for `LEDA` → VBUS (5 V) *and* 3.9 Ω → 120 Ω; only the resistor half was applied, which
is the worst of the three combinations — 120 Ω only makes sense with 5 V above it.
Against a V<sub>F</sub> of 3.2 V typ you land on the knee of the diode curve at **~3–4
mA per string, ~20 mA total against 110 mA typ**, *dimmer than v1.0.2 managed*.

Expect a visibly dim backlight at 100 % duty, varying panel to panel. **PWM still
modulates it — this is a board defect, not a firmware problem. Do not chase it in
software.**

**v1.3 moves `LEDA` to VBUS**, giving 15 mA/string and 90 mA total, ±11 % across the
whole V<sub>F</sub> band.

## LEDs

Four **WS2812B** (3535) on IO18 via RMT, chained LED1 → LED2 → LED3 → LED4, and out to
**JP3** (3-pin: 3.3 V / GND / `LED_OUT`) for an external strip. Driven by the standard
`LedManagerTask` (`NEO_PIXEL_PIN=18`, `NEO_PIXEL_LENGTH=4`, `WIFI_PIXEL_NUMBER=1`),
same as the stock TFT board.

**They are specified below their own supply minimum on both versions.** The BOM part
is `WS2812B-MINI-V3/W` (`C527089`), rated **3.5 V–5.3 V**, running on a 3.3 V rail
whose own worst case is 3.27–3.42 V. Expect colour mixing to drift at low brightness,
and treat it as unreliable rather than broken.

Moving them to 5 V is *not* available as a workaround: V<sub>IH</sub> would become
0.7 × 5 V = 3.5 V, which a 3.3 V GPIO cannot drive. The identified fix is a die
revision — **`WS2812B-MINI-V6`, `C52941386`, rated 3.3 V–5.3 V**, same package and
same 1.9 mm height — but it is not applied to any BOM yet. Assume V3/W in hand.

### RAPI activity LEDs — v1.6.1 and later

Three discrete LEDs sit beside the RAPI header. **LED5** (green, PWR) is hardwired
across the 3.3 V rail after the buck and reaches no GPIO — deliberately, so that one
glance proves the input, `U6` and `U2` all at once. Firmware cannot see it and should
not model it.

The other two are driven by `src/rapi_activity_led.cpp`:

| | pin | colour | meaning |
|---|---|---|---|
| `LED6` | IO45 | red | RAPI **TX** — retriggerable 30 ms one-shot per byte |
| `LED7` | IO46 | yellow | RAPI **RX** — same one-shot, **solid ON** while the link is down |

Both drive the anode through 1 kΩ to GND, so both are **active HIGH, and that is a
hardware constraint rather than a convention.** IO45 selects the VDD_SPI voltage and
IO46 gates the ROM boot message; both must latch 0 at reset, and an LED to GND cannot
lift either above its forward voltage, so the internal pull-downs still sample 0.
Wired the other way — resistor to 3.3 V, cathode on the pin — **the board does not
boot.** Never invert the sense and never add an external pull-up.

The one-shot is not decoration. At 115200 baud a character lasts 87 µs against the
~20 ms an eye needs, so lighting the raw UART line would be invisible — the same
reason FTDI parts expose `TXLED#`/`RXLED#` through a one-shot instead of the line
itself. On the RX side it would also be marginal electrically: the node's low is the
controller's V_OL plus `D3`'s 0.7 V ≈ 0.9 V, which a yellow will not light at all.

Solid-on-fault outranks the blink, because a dead link must not look like an idle
bus: the gateway keeps transmitting into an unresponsive controller, so without it
LED6 would flick merrily while LED7 sat dark.

## microSD — fitted

**J2 (DM3AT-SF-PEJM5) and R33 are fitted** on both versions — `parts.py:65` has
`DNP = {'JP5', 'JP6', 'J1'}` and J2 is not in it. Design doc §11: *"J2 and R33 are
fitted — the microSD was asked for, and R33 is not optional alongside it."*
IO38–41 are live pins, not documentation.

Wired for **SDMMC 1-bit**: CLK IO38, CMD IO39, D0 IO40, CD IO41, with 10 kΩ pull-ups
on CMD, D0 and DAT3.

- **R33 is functional, not decoration.** It holds `DAT3` high. If `DAT3` floats low
  during init the card latches into **SPI mode and never answers SDMMC**, which
  presents as a dead socket on good hardware. Check this first if bring-up fails.
- **1-bit is the only mode available.** DAT1/DAT2 reach no GPIO on either version.
  (**v1.3** adds pull-ups on them, R44/R45, but still no GPIO.)
- **Card-detect on IO41 is active low** and needs a pull-up: internal on v1.2,
  external R42 on v1.3.
- **A card swap requires opening the enclosure.** The socket mouth sits 1.46 mm
  inboard and push-push needs a full card length of travel. Design for a card that
  stays in, not one that gets rotated.
- **Any write can be cut mid-flight.** There is no power-fail warning on this board,
  and hold-up is ~400 µs against an SD card's 250 ms worst-case busy. This is not
  fixable in hardware — the on-card format has to be survivable (fixed-size records,
  sequence number, CRC; a torn record fails CRC and is skipped).

### Firmware

`ENABLE_SD_CARD`, `src/sd_card.*` (mount + presence) and `src/sdlog_store.*` (the
log itself).

**The card is preferred, not required.** With a card fitted the energy log lives on
it; with an empty slot the existing internal-flash tsdb path is used unchanged. The
two are never written together — one store owns the samples at a time, so doubling
the wear would buy nothing. `/status` reports `sd_status` and `sd_log`, because on a
board whose card sits behind a sealed enclosure, *"is it logging to the card or has
it quietly fallen back to flash?"* is not answerable by looking at it.

Fallback is also the runtime failure path: pulling a live card, or any write error,
marks the store not-ready and the next sample goes to flash. Card-detect is sampled
from the main loop once a second (`sd_card_loop()`), so a pull is acted on within a
second and a re-inserted card is mounted again without a reboot. A mount is only
attempted on the rising edge of detect, so a card that is present but will not
mount is tried once per insertion, not once a second. Verified on v1.2 hardware:
pull → `[sd] card removed` in under a second; re-insert → remount and the ring
resumes at the next sample with its count intact.

**Cadence on the card is 10 s charging / 60 s idle**, against 1 min / 5 min on
internal flash (`tsdb_sample_interval_ms()` in `tsdb_sample.h`). Wear is the only
reason flash is slow, and the card does not care.

Capacity is 4 194 304 records × 32 B = 128 MB — over a year of continuous charging
at 10 s before the ring wraps, against the internal tsdb's ~100 days in 2.5 MB. The
file is created at full size up front so every later write is an in-place overwrite
rather than a growth that disturbs FAT metadata. Creation is slow (79 s for 128 MB
on a 32 GB card at 1-bit/20 MHz, 221 s on the same card straight after a format),
so it runs in a background task while the logger keeps writing to flash; `/status`
`sd_status` reads `creating log` for the duration. A ring of the wrong size (a
capacity change between builds, or a creation cut short) is discarded and recreated;
its records are not carried over.

**Reading it back.** `/energy/raw?max=500` took 5.9 s on the first bench run. Three
things, in order of blame: the query cursor read each 32-byte record with its own
seek + read (a 512-byte sector fetch over the 1-bit bus per record) -- fixed with
a 4 KB block cache in `sdlog_store`; the web server drains a response
`ARDUINO_MONGOOSE_SEND_BUFFER_SIZE` (256 B) per main-loop pass -- the S3 env sets
it to one lwIP send buffer; and the lwIP TCP buffers are doubled in
`sdkconfig.defaults.s3lcd` since they live in PSRAM here. Same query now: 0.28 s;
2000 points (62 KB) 0.94 s; a 100 KB GUI asset 1.7 s → 1.0 s. `/config` is still
~0.9 s and that is the handler, not the transport.

### Config mirror

`src/config_backup.*`. Every commit of the user config also writes it, secrets
included, to `/sdcard/openevse/config.json` (temp file, rename, fsync). At boot,
a board whose flash holds no config but whose card holds a mirror restores it and
restarts, so a wiped or swapped module comes back on the network by itself. The
mirror is armed only after that boot-time decision, so the housekeeping commits a
default config makes on first boot cannot overwrite a good mirror before it is read.

The mirror is plaintext on a card that sits inside the enclosure. Certificates
(TLS client certs live in LittleFS, referenced by id from the config) are **not**
mirrored yet, so a restored config that names a certificate will need it uploaded
again.

### Format

`POST /sdcard/format` (JSON body, `{}` is fine — Mongoose leaves a body-less POST
hanging) wipes the card, then re-provisions it: fresh ring, then the config mirror.
Asynchronous; the response is `{"msg":"started"}` and `sd_status` walks
`formatting` → `creating log` → `mounted`. `409 busy` while a job runs, `404` with
no card. The GUI has a Format button with a confirmation in the storage table.
Budget several minutes: 4.7 min for the format alone on a 32 GB card, plus the ring.

Automatic format of a card that will not mount is **off by default** and behind
`-D SD_FORMAT_ON_MOUNT_FAIL`: on this board an unmountable card is more often the
DAT3 / R33 wiring than a bad filesystem, and erasing a card on that diagnosis is
the wrong default.

`/energy` serves from whichever store is live, through a small cursor that dispatches
between the two; the JSON is identical either way. Note the consequence: history
written to a card stays on that card, and swapping cards swaps the history with it.

The on-card format is `src/sdlog_record.*` and `src/sdlog_ring.*` — see the storage
section above for why it carries a CRC and a sequence number when the internal path
does not. Both are pure and host-tested; none of it is hardware-validated.

## Storage — where the energy history lives

The board design doc's §7 and the firmware disagree, and the disagreement is worth
resolving on paper before anyone acts on either. §7 says:

> **Do not use LittleFS for time series** — copy-on-write plus wear-levelling means
> write amplification on every append, and there is no indexing. Use a raw partition
> as a circular log of fixed-size records with a sequence number and CRC; a torn
> record fails CRC and is skipped.

`tsdb_energy_logger` writes `/littlefs/energy.tsdb` — a 2.5 MB ring, 1/min while
charging and 1/5 min idle. Checked against what `components/esp_tsdb` actually does,
**most of §7's prescription is already met and one part genuinely is not**:

| §7 asks for | esp_tsdb | |
|---|---|---|
| circular log | 1024-byte blocks of fixed-size records, LRU eviction by index | ✅ |
| fixed-size records | yes, `record_size` fixed at init | ✅ |
| indexing | `tsdb_index.c` maintains a time index | ✅ — the "no indexing" objection is about a naive file, not this |
| sequence number + CRC | `block_magic` only; **no CRC, no sequence** | ❌ |
| raw partition | hosted on LittleFS | ❌ |

So the open item is **torn-write survival**, not the ring structure. `block_magic`
distinguishes a never-written block from a written one; it cannot distinguish a
complete block from one cut in half. That matters more once P3-2 lands, because a
card write can be interrupted at any point (~400 µs of hold-up against a 250 ms
worst-case busy), and it is worth having regardless of where the bytes end up.

Two things constrain the "move it to a raw partition" half:

- **`tsdb_energy_logger` is shipped.** `openevse_wifi_tft_v1` and
  `openevse_wifi_v1_16mb` both build with `ENABLE_TSDB` and are hardware-validated.
  Changing the storage backend is not an S3-board change, and it carries a data
  migration for units already holding history.
- **`openevse_16mb.csv` is fully allocated** — app0 6 MB, app1 6 MB, spiffs 3.5 MB,
  coredump 64 KB, ending exactly at 0x1000000. There is no room for a new raw
  partition without repartitioning, which on shipped units means an OTA repartition.

**Recommendation, in order:** add the sequence number and CRC to the record format
first — it is the part that actually buys something, it is backend-independent, and
it can be done without touching the partition table. Treat the raw-partition move as
a separate question, driven by measured wear rather than by principle: at 1/min §7's
own arithmetic gives ~10 years and 244 years of wear headroom, so LittleFS
amplification is survivable either way on internal flash.

**On internal flash vs. the card:** §7's numbers favour keeping the append path on
internal flash — a 4 KB sector fills every 21 h at this cadence. The card is better
suited to what you physically remove: bulk retention and offload. Nothing about
P3-2 argues for moving the hot path onto it.

Whichever way it goes, **update the other document.** Right now a reader who finds
§7 first will conclude the firmware is wrong, and a reader who finds the firmware
first will conclude §7 is stale. Neither is quite true.

## Host interfaces

**JP1 — OpenEVSE RAPI** (JST-PH-6, mates with the controller's *FTDI Serial* header;
those labels are cable-centric, so "RX" is the pin the controller drives). This is
`RAPI_PORT = Serial1`:

| Pin | Controller | This board |
|---|---|---|
| 1 | Ground | GND |
| 2 | N/C | — |
| 3 | +5 V | `VBUS_EVSE` (powers this board) |
| 4 | RX | → D3 → IO2 |
| 5 | TX | ← IO1 |
| 6 | N/C | — |

**JP2 — AUX serial** — same connector and shape, on UART2 (IO47/IO48). Unused by
firmware.

**JP5 — programming pads**, 6 pads, silkscreened with **board-side** names: `GND` /
`IO0` / `3V3` / `RXD` / `TXD` / `RST`. An adapter's TX goes to the pad marked `RXD`.
No auto-reset circuit — drive DTR/RTS yourself or use the buttons. This is UART0 =
`DEBUG_PORT = Serial`.

**USB-C (JP4)** — native USB-Serial-JTAG on IO19/20 behind a USBLC6-2SC6. CC1 and CC2
have 5.1 kΩ pull-downs (sink only, no PD). Normal flashing path.

The board builds with `ARDUINO_USB_MODE=1` **and `ARDUINO_USB_CDC_ON_BOOT=1`**, so
`Serial` — and therefore `DEBUG_PORT` — is the **USB CDC device**: flash and read logs
over the one cable. Without CDC-on-boot, `Serial` would resolve to UART0 and you would
flash over USB-C while getting no output on it, needing a UART adapter on JP5 (which
has no auto-reset circuit).

Consequences of that choice:

- **UART0 is `Serial0`**, not `Serial`, if you ever want the JP5 pads from firmware.
- **ROM and second-stage bootloader output still goes to the JP5 pads**, since CDC
  only exists once the app is running. JP5 remains the place to watch a boot loop or
  a panic before `setup()`.

Boot buttons: **SW1 = BOOT** (IO0), **SW2 = RESET** (EN).

## First bring-up — what to check, and what not to chase

None of this firmware has run on the hardware. Written while it was fresh, so the
first person to power a board does not have to re-derive it.

### Do not chase these — they are known board behaviour

| Symptom | Cause |
|---|---|
| Backlight visibly dim at 100 % duty | v1.2 defect: `LEDA` on 3.3 V with the 120 Ω ballast. PWM still modulates. Firmware compensation is not the answer. |
| WS2812 colours drift at low brightness | The `-MINI-V3/W` die is rated 3.5 V and runs on 3.3 V. Unreliable, not broken. |
| No firmware log on the JP5 pads | Expected: `Serial` is USB CDC. JP5 carries ROM/bootloader output only. |
| Firmware cannot tell USB from EVSE power | v1.2 has no sense line at all. `PWR_ST` is v1.3-only. |
| No pilot voltage anywhere | There is no ADC on either revision. Pilot state comes over RAPI. |

### Silkscreen for the next fab — every part with a direction

v1.2 marks JP5's pads and nothing else. J1 is DNP and gets hand-soldered, the
WS2812B footprint is the prime suspect for the dead LED_DIN net, and the
JST-PH headers carry cable-centric names — so the orientation of most of what a
person touches on this board is currently inferred from the schematic. Each of
these needs a mark on the silk, not in a document:

| Ref | Part | What to mark | Why it matters |
|---|---|---|---|
| **J1** | ER-TFT035-6 FPC | Pin 1 **and** contact side (contacts up or down) | DNP, hand-fitted. A 0.5 mm FPC seats either way round; reversed or contacts-down it inits clean over a write-only bus and shows nothing. Add a "1" at the pin-1 end and an arrow for the cable's contact face. |
| **LED1–LED4** | WS2812B-MINI 3535 | Pin-1 dot per LED plus a DIN → DOUT arrow along the chain | LED_DIN reads 1.1 V on every board with IO18 driven high: the footprint's pin mapping is suspect. A pin-1 mark lets that be checked against the die with a loupe instead of a redesign. |
| **JP3** | LED strip out | `3V3` / `GND` / `DOUT` per pin | Three pins, no key; a strip plugged in backwards puts 3.3 V on its data line. |
| **JP1**, **JP2** | JST-PH-6 RAPI / AUX | Pin 1, plus **board-side** names (`GND` / `—` / `5V_IN` / `RX`(IO2) / `TX`(IO1) / `—`) | The current names are the controller cable's. "RX" on this board is the pin the controller *drives*, which is exactly the thing someone probing it gets wrong. JP5 already does this the right way. |
| **D3**, **D2** | series diodes on the RAPI RX / AUX RX nets | Cathode band | Anode is on the GPIO side; fitted backwards the UART simply never receives, with no other symptom. |
| **BT1** | CR2032 holder | `+` on the holder outline | Cell in backwards leaves the RTC with no backup and the firmware reporting a lost clock only after the next power cut. |
| **SW1**, **SW2** | tact switches | `BOOT` / `RESET` | Both are unlabelled; the difference is holding one while pressing the other. |
| **JP4** | USB-C | nothing | Keyed. |
| **J2** | microSD | nothing | Keyed by the push-push cage. |
| **U.FL** | antenna | `ANT` | The connector is unambiguous; the label saves the fitter searching for it under the module. |
| **ICs** (DS3231, MCP9808, TPS2116, USBLC6) | — | Pin-1 dot on the courtyard | Standard, and the two I²C parts are the ones a rework touches. |

Also worth a silk line: **`v1.2` / `v1.3` revision text** where a phone photo will catch
it. The two revisions differ in the backlight rail and three GPIOs, and the difference
is not visible on the board.

### Order of operations

1. **Flash over USB-C** and watch the same cable (`pio device monitor`). If nothing
   appears, put a UART adapter on JP5 — that carries ROM output and will show a boot
   loop or a panic that CDC never gets a chance to report.
2. **Panel.** The bus is unproven silicon-to-panel. If it misbehaves at init, the one
   wiring choice that departs from the datasheet is pin 35 `RD` tied to GND. `TFT_RST`
   is a real pin here, so the panel can be re-inited without a reboot — use that
   rather than power-cycling.
3. **RAPI.** If the controller link is dead, check the `RAPI_RX_PULLUP` path first:
   without it IO2 floats and the port reads as silent. Confirm `TX1`/`RX1` did not get
   "cleaned up" out of `platformio.ini` — the core's S3 fallback puts RX1 on IO15, the
   backlight FET.
4. **I²C.** MCP9808 at `0x18` and DS3231 at `0x68` on IO8/IO9. `[rtc] DS3231 found` in
   the boot log means the driver is talking to it.
5. **RTC.** Let it sync over NTP, pull power for a few minutes, and check the clock
   survives. `[rtc] system clock seeded from RTC` on the next boot is the success
   case. A flat or missing cell logs the oscillator-stop flag instead and must present
   as *no* time, never as 1970.
6. **SD.** `sd_status` in `/status` should read `mounted`. **First boot with a fresh
   card creates a 32 MB file and that is the one slow step in this whole list** — time
   it, and if it is unreasonable, `SDLOG_CAPACITY` is the knob. If the mount fails on a
   card that works elsewhere, check R33/DAT3 before suspecting anything else.

### Fallback paths worth deliberately breaking

These are the paths that only run when something goes wrong, so they are the ones
that will never get exercised by accident:

- **Boot with an empty slot.** Should log to internal flash, `sd_status` = `no card`,
  and `/energy` should still serve.
- **Pull the card while it is running.** Next sample should go to flash, and it should
  not retry the card afterwards.
- **Boot with a card holding a half-written record.** Recovery should resume after the
  last good record rather than restarting the ring. The unit tests cover the logic;
  this proves the file layer agrees with them.

### Measurements owed

- **`SPI_FREQUENCY=80000000`.** Ships at 40 MHz. The result decides whether v1.3 needs
  33 Ω series termination on `TFT_SCLK`/`TFT_MOSI` — there is none anywhere on the bus
  today, and v1.3 is designed but not ordered, so that window shuts at fab. **Do not
  backport a clock bump to the stock TFT board on the strength of an S3 result**: same
  controller, different panel part and flex path.
- **First-boot ring preallocation time**, as above.
- **Internal-heap headroom at boot.** `SPIRAM_MALLOC_RESERVE_INTERNAL` is 0, so the
  LVGL draw buffer competes with everything else. The `[panel] display up …` line
  reports free internal heap; a failure there prints the largest free block.

## Minor deviation worth knowing

Panel pin 35 `RD` is tied to **GND**; the datasheet asks for it to be fixed to IOVCC
when unused. It is not used at all in 4-wire serial mode, so this should be harmless —
but if the panel misbehaves at init, it is the one wiring choice that departs from the
datasheet.

## Building

```
pio run -e openevse_s3_lcd          # production
pio run -e openevse_s3_lcd_dev      # + debug flags, DEBUG_PORT on UART0
```

Flash over USB-C (`pio run -e openevse_s3_lcd -t upload`), or OTA once it is on the
network. `pio device monitor` on the same USB-C cable gives the firmware log
(`DEBUG_PORT` = USB CDC); the JP5 pads carry ROM/bootloader output. Partition table is `openevse_16mb.csv`, the same 6 MB/6 MB dual-OTA layout
the 16 MB WROOM and stock TFT boards use.
