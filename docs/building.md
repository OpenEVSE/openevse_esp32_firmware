# Building the firmware (developers)

This firmware builds with [PlatformIO](https://platformio.org/). Most envs build
out of the box with `pio run -e <env>`. The one wrinkle is that the tree spans
**two incompatible Arduino-ESP32 cores**, which need a little care for local
builds — see [Two-core tree](#two-core-tree) below.

## Quick start

```bash
pip install -U platformio          # or use the VS Code PlatformIO extension

# Build the default 4MB WiFi gateway
pio run -e openevse_wifi_v1

# Build + flash a connected board
pio run -e openevse_wifi_v1 -t upload
```

The web GUI is a git submodule (`gui-v2`); the build embeds its compiled assets.
If you see *"GUI files not found"* run `git submodule update --init`, then
`cd gui-v2 && npm install && npm run build` (Node.js required). See
[`scripts/extra_script.py`](../scripts/extra_script.py); `GUI_NAME=<dir>` selects
an alternate GUI checkout.

## Two-core tree

The boards split across two PlatformIO platforms:

| Boards | Platform | Arduino / IDF | Why |
|---|---|---|---|
| 4MB (`openevse_wifi_v1`, gateways, huzzah, …) | `espressif32@6.12.0` | core **2.x** / IDF4 | Keeps the IDF4 image small enough for **dual-slot OTA** on 4MB flash. |
| 16MB / P4 (`openevse_p4`, `openevse_wifi_tft_v1`, `openevse_wifi_tft_v1_dev`) | `${common.platform_core3}` (pioarduino) | core **3.x** / IDF5 | Newer silicon (ESP32-P4, larger flash) needs the core-3 toolchain. |

Shared `src/` code that touches APIs which changed between cores (e.g. LEDC,
mbedTLS cert parsing) is version-guarded on `ESP_ARDUINO_VERSION` /
`MBEDTLS_VERSION_NUMBER` so it compiles on both.

### Local builds: use `scripts/pio`

The two platforms declare ~13 package names in common (the Arduino framework,
esp-idf, the toolchains, several `tool-*`) but pin **different versions**. They
all install into one `PLATFORMIO_CORE_DIR` (`~/.platformio`), so building a
core-2 env right after a core-3 env — or vice versa — **evicts and re-downloads**
those packages every time you switch. (CI is immune: each job runs on a fresh,
isolated runner.)

To avoid the thrash locally, build through the wrapper, which routes each core to
its own `PLATFORMIO_CORE_DIR`:

```bash
scripts/pio run -e openevse_p4 -t upload   # core-3  -> ~/.platformio-core3
scripts/pio run -e openevse_wifi_v1        # core-2  -> ~/.platformio (default)
scripts/pio device monitor                 # any other pio subcommand, passed through
```

`scripts/pio` is a transparent `pio` drop-in: it reads `-e <env>`, picks the
matching core dir, and execs the real `pio` with your args unchanged. Notes:

- **core-2 stays in the default `~/.platformio`**, so bare `pio`, CI, and IDE
  integrations are unaffected.
- **core-3 goes to `~/.platformio-core3`** (override with `PIO_CORE3_DIR`). The
  first core-3 build populates it once (downloads its toolchain/framework); after
  that, switching cores never re-downloads.
- It **refuses** to build core-2 and core-3 envs in a single invocation (they
  can't share a core dir) — split them into two commands.

Trade-off: the separate core-3 dir costs a few GB of disk. That's the price of
never re-downloading on a core switch.

## Flashing the ESP32-P4 (Guition board)

```bash
scripts/pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5
```

The P4 enumerates as an *Espressif USB JTAG/serial debug unit* (USB `303a:1001`).
Its app console does **not** reliably reach the host over USB on this board (HWCDC
limitation) — for crash debugging, capture the panic from the board's UART0 and
decode addresses with `riscv32-esp-elf-addr2line -e .pio/build/openevse_p4/firmware.elf <addr>`.

## EEZ Studio LVGL UI (P4 display)

The on-device LVGL UI is designed in EEZ Studio and exported as C into
`eez-ui-export/`. It's compiled in only for `openevse_p4` (behind
`-D ENABLE_EEZ_UI`) via the single gated unit `src/display_p4/eez_ui_unit.c`, so
it never affects other envs. After re-exporting from EEZ Studio ("Build"), re-sync
the generated sources into `eez-ui-export/` and rebuild. The
`src/display_p4/eez_compat/` shim resolves the export's `#include <lvgl/lvgl.h>`
to the firmware's `<lvgl.h>` so re-exports build unmodified.
