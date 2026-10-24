# Building the firmware

This firmware builds with [PlatformIO](https://platformio.org/). Most envs build
out of the box with `pio run -e <env>`. The one wrinkle is that the tree spans
**two incompatible Arduino-ESP32 cores**, which need a little care for local
builds — see [Two-core tree](#two-core-tree) below.

## Bootstrap (first time only)

The web GUI is a git submodule and must be built **before** the firmware — the
build embeds its compiled assets into the binary
(see [`scripts/extra_script.py`](../../scripts/extra_script.py)).

```bash
git submodule update --init --recursive
cd gui-nightshift && npm install && npm run build && cd ..
```

Node.js 20+ and npm are required. The default UI is the **`gui-nightshift`**
submodule; setting `GUI_NAME=<dir>` selects an alternate GUI checkout.

## Quick start

```bash
pip install -U platformio          # or use the VS Code PlatformIO extension

# Build the default 4MB WiFi gateway
pio run -e openevse_wifi_v1

# Build + flash a connected board
pio run -e openevse_wifi_v1 -t upload
```

Common alternative envs (see `platformio.ini` for the full list):

```bash
pio run -e adafruit_huzzah32
pio run -e nodemcu-32s
pio run -e olimex_esp32-gateway-e      # wired Ethernet
pio run -e openevse_wifi_tft_v1        # TFT touchscreen
pio run -e openevse_wifi_v1_rc522      # MFRC522/RC522 on SPI (instead of PN532)
pio run -e wt32-eth01                  # Ethernet (WT32)
pio run -e olimex_esp32-poe-iso        # Ethernet + PoE
```

Build artifacts land in `.pio/build/<env>/firmware.bin`. The first build
downloads the ESP32 toolchain (~500 MB) and can take 15–45 minutes — do not
cancel it; incremental builds take 2–5 minutes.

If *"GUI files not found"* appears, the submodule is missing or unbuilt — run
the bootstrap steps above. If `HTTPClientError` appears, PlatformIO cannot
reach its package registry (network restriction).

## Platform

Every firmware env builds on the official PlatformIO `espressif32` platform,
pinned once in `[env]` in `platformio.ini` (currently `espressif32@8.0.0`, which
ships Arduino-ESP32 **3.x** on ESP-IDF 5.x). There is no separate core-2 / core-3
split: envs inherit the platform and must not override it (the `native_*` host
test envs aside).

Shared `src/` code that touches APIs which changed between Arduino cores (e.g.
LEDC, mbedTLS cert parsing) is still version-guarded on `ESP_ARDUINO_VERSION` /
`MBEDTLS_VERSION_NUMBER`; those guards are harmless on core 3 and can be removed
as the old-core paths are retired.

Because the 4MB boards moved onto Arduino core 3 / IDF5, watch the flash-usage
check in CI for the 4MB envs: the old core-2 / IDF4 image was what kept them
inside **dual-slot OTA** limits on 4MB flash.

### Local builds

```bash
pio run -e openevse_wifi_v1_16mb
pio run -e openevse_wifi_v1
```

`scripts/pio` is kept as a transparent `pio` drop-in (it just locates and execs
the real `pio` binary), so existing commands keep working. Since all envs share
one platform, they share one `PLATFORMIO_CORE_DIR` (`~/.platformio`) and switching
envs no longer re-downloads packages.

## Debug builds

The `_dev` env variants enable serial debug output on the ESP's second serial
port (GPIO2 / `Serial1`):

```bash
pio run -e openevse_wifi_v1_dev -t upload
```

To route debug output to the main serial port instead, change
`-DDEBUG_PORT=Serial1` to `-DDEBUG_PORT=Serial` in `platformio.ini` — note this
interferes with RAPI communication to the controller.

## Flash usage

CI checks every build's flash usage against its app partition budget
(`scripts/check_flash_size.py`), reading the same "Flash: NN.N% (used X bytes
from Y bytes)" line PlatformIO itself prints after linking -- the same number
PlatformIO already relies on to decide whether a build fits, so this can't
disagree with it. A build at 95% or more of its partition emits a
`::warning::`; a build that exceeds it fails outright. Pull requests get a
"Flash usage" comment summarising size and % used per env, and the change
versus the base branch's last build (`scripts/report_flash_size.py`).

Note: that line is an ELF-section total and doesn't include any padding
esptool's elf2image step adds when writing the final `firmware.bin`, so in
principle the on-disk file could be somewhat larger than what's checked here.
Confirming whether that gap is significant (and for which chips) needs
inspecting the actual built artifact; see the discussion on the PR that
introduced this script if you're investigating that.

To check locally after a build:

```bash
set -o pipefail
pio run -e openevse_wifi_v1 | tee /tmp/pio-build.log
python scripts/check_flash_size.py --env openevse_wifi_v1 \
  --log /tmp/pio-build.log \
  --out /tmp/flash-size.json
```

## Upload troubleshooting

- Some boards need manual bootloader mode: hold **BOOT**, press **RESET**,
  release RESET, release BOOT.
- Try a lower upload baud rate if flashing is unreliable.
- If the ESP reboot-loops after an upgrade, erase the flash and re-flash:

```bash
esptool.py erase_flash
```

## Host-side unit tests

Host-side logic and certificate persistence are tested via the `native_test`
environment. Install the OpenSSL development headers and library first (on
Debian/Ubuntu: `sudo apt-get install libssl-dev`). Certificate tests use the real
certificate store with OpenSSL and an isolated EpoxyFS directory.

```bash
pio test -e native_test
```

Test suites live under `test/`. New host-testable logic should land with a
doctest suite alongside it.

To run just the direct certificate ID regression:

```bash
pio test -e native_test -f test_certificate_direct_id
.pio/build/native_test/program
```

The direct binary prints the assertion totals. Run it before building another
suite, since all suites in this environment share that output path.

The full native firmware build is `native_openevse`. To build the host binary with
the LVGL local UI path enabled, use `native_openevse_lvgl`:

```bash
pio run -e native_openevse_lvgl
```

That env enables the LVGL local UI code on the host build. By default it runs in
headless mode so the UI logic is compiled and exercised without requiring ESP32
display hardware.

For interactive local development you can also open the native LVGL output in an
SDL window:

```bash
.pio/build/native_openevse_lvgl/program --lvgl-display window
```

That runtime mode loads SDL2 dynamically when it is available on the host (for
example via `libsdl2-dev` on Debian/Ubuntu) and keeps the default headless path
unchanged for CI and screenshot export.

To dump the sample LVGL screens from the native binary after that build completes:

```bash
mkdir -p /tmp/lvgl-screens
.pio/build/native_openevse_lvgl/program --dump-lvgl-screens /tmp/lvgl-screens
```

That command writes the boot, setup, and charge-state captures as `.ppm` images so
review comments can attach fresh screenshots of the LVGL local UI.

## divert_sim host build

Build the simulator with PlatformIO:

```bash
pio run -e native_simulator
```

This writes the binary to `.pio/build/native_simulator/program`. The
`divert_sim` pytest suite uses that binary automatically (or `./divert_sim` if
present):

```bash
cd divert_sim
pip install -r requirements.txt
pytest -v
```
