# OpenEVSE Remote Display (`openevse_remote_display`)

A standalone wall/desk display for an OpenEVSE station, running this firmware on an
**Elecrow CrowPanel Advance 3.5" V1.3** (ESP32-S3-N16R8, 480x320 ILI9488 SPI panel,
GT911 capacitive touch). No RAPI controller is attached: instead, the
**remote-display client** (`src/remote_display_client.cpp`) polls the station's
HTTP `/status` endpoint and feeds the exact same LVGL screens
(`src/lvgl_tft/`, boot / charge / standby, nightshift + light themes) that the
stock `openevse_wifi_tft_v1` build renders from local data.

## Building / flashing

```sh
pio run -e openevse_remote_display
pio run -e openevse_remote_display -t upload      # USB
# debug build (serial debug on the USB port):
pio run -e openevse_remote_display_dev
```

The environment lives in `platformio.ini` and is composed of reusable fragments:

| Fragment | What it does |
|---|---|
| `elecrow_advance_35_hw_flags` | ILI9488 panel pins/inversion for the CrowPanel Advance 3.5" (TFT_eSPI `USER_SETUP_LOADED`) |
| `lvgl_tft_renderer_flags` | The same LVGL renderer selection (`ENABLE_SCREEN_LVGL_TFT`) used by `openevse_wifi_tft_v1` |
| `touch_gt911_flags` | `ENABLE_TOUCH_GT911` + I2C pins for the GT911 touch controller |
| `remote_display_flags` | `ENABLE_REMOTE_DISPLAY_CLIENT` — swaps the screens' data source from EvseManager to the HTTP client |

## Setup

1. Flash, then join the `openevse-xxxx` AP the display brings up and configure
   WiFi as usual (the QR setup screen works the same as on the stock TFT).
2. Point the display at your station — `remote_display_host` accepts an IP,
   a DNS name, or an mDNS `.local` name:

   ```sh
   curl http://<display-ip>/config \
     -H 'Content-Type: application/json' \
     -d '{"remote_display_host":"openevse-c620.local"}'
   ```

The display polls `http://<remote_display_host>/status` every 5 s
(`REMOTE_DISPLAY_POLL_MS`) and shows "No data from ..." on the charge screen
whenever nothing fresh has arrived for 30 s (`REMOTE_DISPLAY_DATA_VALID_MS`).
The session-elapsed tile ticks locally between polls while charging.

## Touch

The GT911 is polled by LVGL as a pointer device (`src/lvgl_tft/touch_gt911.cpp`,
a self-contained `Wire` driver). The screens are read-only; a touch anywhere
wakes the backlight / leaves the dimmed standby screen, exactly like an EVSE
state change does.

## Orientation

Two build flags control orientation, and they pair up:

| Panel right-side-up | Panel upside-down |
|---|---|
| `-D TFT_ROTATION=3` (default) | `-D TFT_ROTATION=1` |
| touch defaults (`SWAP_XY=1 MIRROR_X=1 MIRROR_Y=0`) | `-D TOUCH_GT911_MIRROR_X=0 -D TOUCH_GT911_MIRROR_Y=1` |

If the image is upside down on your unit, switch both columns.

## What still runs

This is the full firmware minus nothing at compile time — the web UI, OTA,
MQTT, etc. all still work on the display itself (useful for config + updates).
The EVSE-side subsystems (RAPI, divert, OCPP, ...) are simply idle because no
controller is connected; the screens never read from them in this build.
