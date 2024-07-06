# Development Guide

## Compiling and uploading firmware

It is necessary to download and build the static web assets for the GUI before compiling and uploading to the ESP.

### Building the GUI static assets

The GUI static web assets are minified and compiled into the firmware using a combination of Webpack and a [custom python build script](../scripts/extra_script.py).

You will need Node.js and npm installed: <https://nodejs.org/en/download/>

In addition, the GUI is now maintained in a [separate repository](https://github.com/OpenEVSE/openevse-gui-v2/) and is included as a Git submodule.

If the `gui` directory is empty, use the following to retrieve the GUI source and fetch the dependencies:

```bash
git submodule update --init --recursive
cd gui
npm install
```

To 'build' the GUI static assets, run the following from the `gui` directory:

```bash
npm run build
```

Now you are ready to compile and upload to the ESP32.

### Compile and upload using PlatformIO

For info on the Arduino core for the ESP32 using PlatformIO, see: <https://docs.platformio.org/en/latest/platforms/espressif32.html>

#### 1. Install PlatformIO

PlatformIO can be installed as a command line utility (PlatformIO Core), a standalone IDE (PlatformIO IDO) or as an integration into other popular IDEs. Follow the [installation instructions](https://platformio.org/install).

#### 2. Clone this repo

```bash
git clone https://github.com/OpenEVSE/ESP32_WiFi_V4.x.git
```

#### 3. Compile & upload

- If necessary, put the ESP into bootloader mode. See [documentation on boot mode selection here](https://github.com/espressif/esptool/wiki/ESP32-Boot-Mode-Selection).
  - Some boards handle entering bootloader mode automatically. On other ESP boards, like the Adafruit HUZZAH 32 Breakout board, you have to press and hold a `boot` button then press a `reset` button, as [seen here](https://learn.adafruit.com/huzzah32-esp32-breakout-board/using-with-arduino-ide#blink-test-3-13).

Compile and upload using PlatformIO. To compile against the default env (for the ESP_WROVER_KIT, [docs](https://docs.platformio.org/en/latest/boards/espressif32/esp-wrover-kit.html)), run:

```bash
pio run -t upload
```

If you are using a different development board, you can specify one of the envs setup in `platformio.ini`, for example:

```bash
pio run -e adafruit_huzzah32 -t upload
```

Build artifacts will be in `.pio/build/your_openevse_env`

To clean an existing build, `pio run -e your_openevse_board -t`

> To enable OTA updates, first upload via serial using the dev environment. This enables OTA enable build flag

---

## Troubleshooting

### Uploading issues

- Double check device is in bootloader mode
- Try reducing the upload ESP baudrate
- Erase flash: If you are experiencing ESP hanging in a reboot loop after upload it may be that the ESP flash has remnants of previous code (which may have the used the ESP memory in a different way). The ESP flash can be fully erased using [esptool](https://github.com/espressif/esptool). With the unit in bootloader mode run:

```bash
esptool.py erase_flash
```

Output:

```text
esptool.py v1.2-dev
Connecting...
Running Cesanta flasher stub...
Erasing flash (this may take a while)...
Erase took 8.0 seconds
```

### Fully erase ESP

To fully erase all memory locations we need to upload a blank file to each memory location:

`esptool.py write_flash 0x000000 blank_1MB.bin 0x100000 blank_1MB.bin 0x200000 blank_1MB.bin 0x300000 blank_1MB.bin`

### View serial debug

To help debug, it may be useful to enable serial debug output. To do this upload using `openevse_dev` environment e.g.

`pio run -t upload -eopenevse_dev`

The default is to enable serial debug on serial1 the ESP's 2nd serial port. You will need to connect a debugger to the ESP serial1 Tx pin (GPIO2).

To change to use serial0 (the main ESP's serial port) change `-DDEBUG_PORT=Serial1` to `-DDEBUG_PORT=Serial` in `platformio.ini`. Note that using serial 0 will adversely effect RAPI communication with the openevse controller.
