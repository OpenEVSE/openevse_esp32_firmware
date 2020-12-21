# OpenEVSE WiFi Gateway v4

> **_NOTE:_** Breaking change! This release requires a minimum of [7.1.2](https://github.com/lincomatic/open_evse) of the OpenEVSE firmware, features may not behave as expected on older firmware.

For the v3 that is compatible with previous EVSE firmwares, see the [v3 branch](https://github.com/OpenEVSE/ESP32_WiFi_V3.x/tree/v3). For the older v2 ESP8266 version (pre June 2020), see the [v2 firmware repository](https://github.com/openevse/ESP8266_WiFi_v2.x/)

Instructions on updating the OpenEVSE firmware see [How to Load OpenEVSE Firmware (WinAVR)
](https://openevse.dozuki.com/Guide/How+to+Load+OpenEVSE+Firmware+(WinAVR)/7?lang=en)

---

![main](docs/main2.png)

The WiFi gateway uses an **ESP32** which communicates with the OpenEVSE controller via serial RAPI API. The web UI is served directly from the ESP32 web server and can be controlled via a connected device on the local network.

Wired Ethernet connection is possible using [ESP32 Gateway](docs/wired-ethernet.md)

**[Live UI demo](https://openevse.openenergymonitor.org)**

## Features

- Web UI to view & control all OpenEVSE functions
  - Start / pause
  - Delay timer
  - Time limit
  - Energy Limit
  - Adjust charging current
- MQTT status & control
- Log to Emoncms server e.g [data.openevse.org](http://data.openevse.org) or [emoncms.org](https://emoncms.org)
- 'Eco' mode: automatically adjust charging current based on availability of power from solar PV or grid export
- OhmConnect integration (California USA only)

## Requirements

### OpenEVSE / EmonEVSE charging station

- Purchase via: [OpenEVSE Store (USA/Canda)](https://store.openevse.com) | [OpenEnergyMonitor (UK / EU)](https://shop.openenergymonitor.com/evse/)
- OpenEVSE FW [v7.1.2+ required](https://github.com/OpenEVSE/open_evse/releases)
- All new OpenEVSE units are shipped with ... *TBD*

### ESP32 WiFi Module

- **Note: WiFi module is included as standard in most OpenEVSE units**
- Purchase via: [OpenEVSE Store (USA/Canda)](https://store.openevse.com/collections/frontpage/products/openevse-wifi-kit) | [OpenEnergyMonitor (UK / EU)](https://shop.openenergymonitor.com/openevse-etherent-gateway-esp32/)
- See [OpenEVSE WiFi setup guide](https://openevse.dozuki.com/Guide/OpenEVSE+WiFi+%28Beta%29/14) for WiFi module hardware connection instructions

### Web browsing device

- Mobile phone, tablet, desktop computer, etc.: any device that can display web pages and can network via WiFi.
*Note: Use of Internet Explorer 11 or earlier is not recommended*

---

## Contents

<!-- toc -->

- [User Guide](#user-guide)
- [Hardware](#hardware)
  - [WiFi Setup](#wifi-setup)
  - [Charging Mode: Eco](#charging-mode-eco)
    - [Eco Mode Setup](#eco-mode-setup)
  - [Services](#services)
    - [Emoncms data logging](#emoncms-data-logging)
    - [MQTT](#mqtt)
      - [OpenEVSE Status via MQTT](#openevse-status-via-mqtt)
    - [RAPI](#rapi)
      - [RAPI via web interface](#rapi-via-web-interface)
      - [RAPI over MQTT](#rapi-over-mqtt)
      - [RAPI over HTTP](#rapi-over-http)
    - [HTTP API](#http-api)
    - [Tesla API](#tesla-api)
    - [OhmConnect](#ohmconnect)
  - [System](#system)
    - [Authentication](#authentication)
    - [Hardware reset](#hardware-reset)
    - [Firmware update](#firmware-update)
      - [Via Web Interface](#via-web-interface)
      - [Via Network OTA](#via-network-ota)
      - [Via USB Serial Programmer](#via-usb-serial-programmer)
- [Development guide](#development-guide)
  - [Compiling and uploading firmware](#compiling-and-uploading-firmware)
    - [Building the GUI static assets](#building-the-gui-static-assets)
    - [Compile and upload using PlatformIO](#compile-and-upload-using-platformio)
      - [1. Install PlatformIO](#1-install-platformio)
      - [2. Clone this repo](#2-clone-this-repo)
      - [3. Compile & upload](#3-compile--upload)
  - [Troubleshooting](#troubleshooting)
    - [Uploading issues](#uploading-issues)
    - [Fully erase ESP](#fully-erase-esp)
    - [View serial debug](#view-serial-debug)
- [About](#about)
- [Licence](#licence)

<!-- tocstop -->

---

# User Guide

# Hardware

Most ESP32 boards can be used (see platfromio.ini for full list of supported boards), however the two boards which are best supported and easiest to use are:

- Adafruit Huzzah32
- [Olimex ESP32 Gateway (Wired Ethernet)](docs/wired-ethernet.md)

## WiFi Setup

On first boot, OpenEVSE should broadcast a WiFi access point (AP) `OpenEVSE_XXXX`. Connect your browser device to this AP (default password: `openevse`) and the [captive portal](https://en.wikipedia.org/wiki/Captive_portal) should forward you to the log-in page. If this does not happen, navigate to [http://openevse](http://openevse), [http://openevse.local](http://openevse.local) or [http://192.168.4.1](http://192.168.4.1)

> **_NOTE:_** You may need to disable mobile data if connecting via a mobile

> **_NOTE:_** Use of Internet Explorer 11 or earlier is not recommended

![Wifi connect](docs/wifi-connect.png) ![Wifi setup](docs/wifi-scan.png)

- Select your WiFi network from list of available networks
- Enter WiFi Passkey, then click `Connect`

- OpenEVSE should now connect to local WiFi network
- Re-connect your browsing device to local WiFi network and connect to OpenEVSE using [http://openevse.local](http://openevse.local), [http://openevse](http://openevse) or local IP address.

**If connection / re-connection fails (e.g. network cannot be found or password is incorrect) the OpenEVSE will automatically revert back to WiFi access point (AP) mode after a short while to allow a new network to be re-configured if required. Re-connection to existing network will be attempted every 5 minutes.**

*Holding the `boot / GPIO0` button on the ESP8266 module for about 5s will force WiFi access point mode. This is useful when trying to connect the unit to a new WiFi network. If the unit cannot connect to a WiFi network it will return to AP mode before retrying to connect*

---

## Charging Mode: Eco

'Eco' charge mode allows the OpenEVSE to start/stop and adjust the charging current automatically based on an MQTT. This feed could be the amount of solar PV generation or the amount of excess power (grid export). 'Normal' charge mode charges the EV at the maximum rate set.

![eco](docs/eco.png)

When Eco Mode is enabled:

- Charging will begin when solar PV gen / grid excess > 1.4kW (6A)
- Charging will pause when solar PV gen / grid excess < 1.4kW (6A)
- Smoothing algorithm is used to avoid rapid state transitions
- Eco Mode is persistent between charging sessions
- Eco Mode can be enabled / disabled via MQTT  

A [OpenEnergyMonitor Solar PV Energy Monitor](https://guide.openenergymonitor.org/applications/solar-pv/) can be used to monitor solar PV system and provide an MQTT feed to the OpenEVSE for 'Eco' mode charging.

### Eco Mode Setup

- Enable MQTT Service
- [emonPi MQTT credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt) should be pre-populated
- Enter solar PV generation or Grid (+I/-E) MQTT topic e.g. Assuming [standard emonPi Solar PV setup](https://guide.openenergymonitor.org/applications/solar-pv/), the default MQTT feeds are:
  - Grid Import (positive Import / Negative export*): `emon/emonpi/power1`
  - Solar PV generation (always postive): `emon/emonpi/power2`

> **_NOTE:_** 'Grid' feed should include the power consumed by the EVSEE**

**CT sensor can be physically reversed on the cable to invert the reading.*

[MQTT Explorer](http://mqtt-explorer.com/) can be used to view MQTT data. To learn more about MQTT see [MQTT section of OpenEnergyMonitor user guide](https://guide.openenergymonitor.org/technical/mqtt/).

Divertmode can be controlled via mqtt

Topic: `<base-topic>/divertmode/set`
Value: `1` = Normal or `2` = Eco
  
---

## Services

![services](docs/services.png)

### Emoncms data logging

OpenEVSE can post its status values to [emoncms.org](https://emoncms.org) or any other  Emoncms server (e.g. emonPi) using [Emoncms API](https://emoncms.org/site/api#input). Data will be posted every 30s.

Data can be posted using HTTP or HTTPS.

### MQTT

MQTT and MQTTS (secure) connections are supported for status and control.

At startup the following message is published with a retain flag to `openevse/announce/xxxx` where `xxxx` is the last 4 characters of the device ID. This message is useful for device discovery and contans the device hostname and IP address.

```json
{"state":"connected","id":"c44f330dxxad","name":"openevse-55ad","mqtt":"emon/openevse-55ad","http":"http://192.168.1.43/"}
```

For device discovery you should subscribe with a wild card to `openevse/announce/#`

When the device disconnects from MQTT the same message is posted with `state":"disconnected"` (Last Will and Testament).

All subsequent MQTT status updates will by default be be posted to `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID. This base-topic can be changed via the MQTT service page.

#### OpenEVSE Status via MQTT

OpenEVSE can post its status values (e.g. amp, wh, temp1, temp2, temp3, pilot, status) to an MQTT server. Data will be published as a sub-topic of base topic, e.g `<base-topic>/amp`. Data is published to MQTT every 30s.

> **_NOTE:_** The default `<base-topic>` is `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID

MQTT setup is pre-populated with OpenEnergyMonitor [emonPi default MQTT server credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt).

- Enter MQTT server host and base-topic
- (Optional) Enter server authentication details if required
- Click connect
- After a few seconds `Connected: No` should change to `Connected: Yes` if connection is successful. Re-connection will be attempted every 10s. A refresh of the page may be needed.

> **_NOTE:_**  `emon/xxxx` should be used as the base-topic if posting to emonPi MQTT server if you want the data to appear in emonPi Emoncms. See [emonPi MQTT docs](https://guide.openenergymonitor.org/technical/mqtt/).

MQTT can also be used to control the OpenEVSE, see RAPI MQTT below.

### RAPI

> **WARNING:_** Directly comunicating with the OpenEVSE is depricated and may be removed from future versions of the firmware

RAPI commands can be used to control and check the status of all OpenEVSE functions. RAPI commands can be issued via the direct serial, web-interface, HTTP and MQTT. We recommend using RAPI over MQTT.

**A full list of RAPI commands can be found in the [OpenEVSE plus source code](https://github.com/OpenEVSE/open_evse/blob/stable/firmware/open_evse/src/rapi_proc.h).**

#### RAPI via web interface

Enter RAPI commands directly into to web interface (dev mode must be enabled), RAPI response is printed in return:

![enable-rapi](docs/enable-rapi.png)

![rapi-web](docs/rapi-web.png)

#### RAPI over MQTT

RAPI commands can be issued via MQTT messages. The RAPI command should be published to the following MQTT:

`<base-topic>/rapi/in/<$ rapi-command> payload`

e.g assuming base-topic of `openevse` the following command will set current to 13A:

`openevse/rapi/in/$SC 13`

The payload can be left blank if the RAPI command does not require a payload e.g.

`openevse/rapi/in/$GC`

The response from the RAPI command is published by the OpenEVSE back to the same sub-topic and can be received by subscribing to:

`<base-topic>/rapi/out/#`

e.g. `$OK`

[See video demo of RAPI over MQTT](https://www.youtube.com/watch?v=tjCmPpNl-sA&t=101s)

#### RAPI over HTTP

RAPI (rapid API) commands can also be issued directly via a single HTTP request.

Using RAPI commands should be avoided if possible. WiFi server API is preferable. If RAPI must be used, avoid fast polling.

*Assuming `192.168.0.108` is the local IP address of the OpenEVSE ESP.*

Eg.the RAPI command to set charging rate to 13A:

[http://192.168.0.108/r?rapi=%24SC+13](http://192.168.0.108/r?rapi=%24SC+13)

To sleep (pause a charge) issue RAPI command `$FS`

[http://192.168.0.108/r?rapi=%24FS](http://192.168.0.108/r?rapi=%24FS)

To enable (start / resume a charge) issue RAPI command `$FE`

[http://192.168.0.108/r?rapi=%24FE](http://192.168.0.108/r?rapi=%24FE)

There is also an [OpenEVSE RAPI command python library](https://github.com/tiramiseb/python-openevse).

### HTTP API

Current status of the OpenEVSE in JSON format is available via: `http://openevse-xxxx/status` e.g

```json
{"mode":"STA","wifi_client_connected":1,"eth_connected":0,"net_connected":1,"srssi":-73,"ipaddress":"192.168.1.43","emoncms_connected":1,"packets_sent":22307,"packets_success":22290,"mqtt_connected":1,"ohm_hour":"NotConnected","free_heap":203268,"comm_sent":335139,"comm_success":335139,"rapi_connected":1,"amp":0,"pilot":32,"temp1":282,"temp2":-2560,"temp3":-2560,"state":254,"elapsed":3473,"wattsec":22493407,"watthour":51536,"gfcicount":0,"nogndcount":0,"stuckcount":0,"divertmode":1,"solar":390,"grid_ie":0,"charge_rate":7,"divert_update":0,"ota_update":0,"time":"2020-05-12T17:53:48Z","offset":"+0000"}
```

Current config of the OpenEVSE in JSON format is available via `http://openevse-xxxx/config` e.g

```json
{"firmware":"6.2.1.EU","protocol":"5.1.0","espflash":4194304,"version":"3.1.0.dev","diodet":0,"gfcit":0,"groundt":0,"relayt":0,"ventt":0,"tempt":0,"service":2,"scale":220,"offset":0,"ssid":"<SSID>","pass":"_DUMMY_PASSWORD","emoncms_enabled":true,"emoncms_server":"https://emoncms.org","emoncms_node":"emonevse","emoncms_apikey":"_DUMMY_PASSWORD","emoncms_fingerprint":"","mqtt_enabled":true,"mqtt_protocol":"mqtt","mqtt_server":"emonpi","mqtt_port":1883,"mqtt_reject_unauthorized":true,"mqtt_topic":"emon/openevse-55ad","mqtt_user":"emonpi","mqtt_pass":"_DUMMY_PASSWORD","mqtt_solar":"emon/solarpv/test","mqtt_grid_ie":"","mqtt_supported_protocols":["mqtt","mqtts"],"http_supported_protocols":["http","https"],"www_username":"open","www_password":"_DUMMY_PASSWORD","hostname":"openevse-55ad","time_zone":"Europe/Lisbon|WET0WEST,M3.5.0/1,M10.5.0","sntp_enabled":true,"sntp_host":"pool.ntp.org","ohm_enabled":false}
```

### Tesla API

> **_NOTE:_** The Tesla API is beta and subject to change

> **_NOTE:_** Polling the Tesla API will keep the car awake which will increase vampire drain, see [Issue #96](https://github.com/OpenEVSE/ESP32_WiFi_V3.x/issues/96).

WiFi firmware V3.2 includes basic Tesla API integration. The HTTP API for this is as follows:

```text
POST {{baseUrl}}/config HTTP/1.1
Content-Type: application/json

{
  "tesla_enabled": true,
  "tesla_username": "username",
  "tesla_password": "password"
}
```

```text
POST {{baseUrl}}/config HTTP/1.1
Content-Type: application/json

{
  "tesla_vehidx": 0
}
```

Example using cURL:

```shell
curl --request POST \
  --url http://openevse-xxxx/config \
  --header 'content-type: application/json' \
  --header 'user-agent: vscode-restclient' \
  --data '{"tesla_enabled": true,"tesla_username": "username","tesla_password": "password"}'
```

Return a list of vehicles associated with the Tesla account e.g

`http://openevse-xxxx/teslaveh`

e.g

```json
{"count:"2,[{"id":"xxxx","name":"tesla1"},{"id":"xxxxx","name":"tesla2"}]}
```

> **_NOTE:_** The vehicle ID starts at zero so the first car will have vi=0*

The SoC and rated range of the Tesla vehicle is now displayed in JSON format via `/status` and posted to MQTT.

### OhmConnect

**USA California only**
[Join here](https://ohm.co/openevse)

**Video - How does it Work**
<https://player.vimeo.com/video/119419875>

-Sign Up
-Enter Ohm Key

Ohm Key can be obtained by logging in to OhmConnect, enter Settings and locate the link in "Open Source Projects"
Example: <https://login.ohmconnect.com/verify-ohm-hour/OpnEoVse>
Key: OpnEoVse

## System

![system](docs/system.png)

### Authentication

Admin HTTP Authentication (highly recommended) can be enabled by saving admin config by default username and password.

> **_NOTE:_** HTTP authentication is required for all HTTP requests including input API

### WiFi Reset

- Hold external button for 10 secs
- Connect to the AP mode WiFi
- Connect to new Wifi network

### HTTP Auth Password reset 

- Hold external button for 10 secs
- Connect to the AP mode WiFi
- click the “WiFi Standalone” button 
- Set the HTTP auth details again

### Hardware Factory Reset

A Hardware Factory reset (all WiFi and services config lost) can de done via:

- The WiFi interface (press and hold external button for 10's to enable AP mode if required)
- By pressing and holding GPIO0 hardware button (on the WiFi module inside enclosure) for 10s.

### Hardware Factory Reset

A Hardware Factory reset (all WiFi and services config lost) can de done via:

- The WiFi interface (press and hold external button for 10's to enable AP mode if required)
- By pressing and holding GPIO0 hardware button (on the WiFi module inside enclosure) for 10s.

> **_NOTE:_** Holding the GPIO0 button for 5s will put the WiFi unit into AP (access point) mode to allow the WiFi network to be changed without losing all the service config*

### Firmware update

Firmware can be updated via the Web UI
See [OpenEVSE Wifi releases](https://github.com/OpenEVSE/ESP32_WiFi_v3.x/releases) for latest stable pre-compiled update releases.

#### Via Web Interface

This is the easiest way to update. Pre-compiled firmware `.bin` files can be uploaded via the web interface: System > Update.

If for whatever reason the web-interface won't load it's possible to update the firmware via cURL:

```shell
curl -F 'file=@firmware.bin'  http://<IP-ADDRESS>/update && echo
```

#### Via Network OTA

The firmware can also be updated via OTA over a local WiFi network using PlatformIO:

```shell
platformio run -t upload --upload-port <IP-ADDRESS>
```

#### Via USB Serial Programmer

[Compatiable USB to Serial Programmer](https://shop.openenergymonitor.com/programmer-usb-to-serial-uart/)

On the command line using the [esptool.py utility](https://github.com/espressif/esptool):

If flashing a new ESP32, flashing bootloader and partitions file is required:

```shell
esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

Then successive uploads can just upload the firmware

```shell
esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x10000 firmware.bin`
```

> **_NOTE:_** If uploading to ESP32 Etherent gateway, use slower baudrate of `115200`

Or with the [NodeMCU PyFlasher](https://github.com/marcelstoer/nodemcu-pyflasher) GUI, available with pre-built executable files for Windows/Mac.

---

# Development guide

## Compiling and uploading firmware

It is necessary to download and build the static web assets for the GUI before compiling and uploading to the ESP.

### Building the GUI static assets

The GUI static web assets are minified and compiled into the firmware using a combination of Webpack and a [custom python build script](scripts/extra_script.py).

You will need Node.js and npm installed: <https://nodejs.org/en/download/>

In addition, the GUI is now maintained in a [separate repository](https://nodejs.org/en/download/package-manager/) and is included as a Git submodule.

If the `gui` directory is empty, use the following to retrieve the GUI source and fetch the dependencies:

```shell
git submodule update --init --recursive
cd gui
npm install
```

To 'build' the GUI static assets, run the following from the `gui` directory:

```shell
npm run build
```

Now you are ready to compile and upload to the ESP32.
<https://github.com/OpenEVSE/ESP32_WiFi_V3.x/blob/master/readme.md>

### Compile and upload using PlatformIO

For info on the Arduino core for the ESP32 using PlatformIO, see: <https://github.com/espressif/arduino-esp32/blob/master/docs/platformio.md>

#### 1. Install PlatformIO

PlatformIO can be installed as a command line utility (PlatformIO Core), a standalone IDE (PlatformIO IDO) or as an integration into other popular IDEs. Follow the [installation instructions](https://platformio.org/install).

#### 2. Clone this repo

```shell
git clone https://github.com/OpenEVSE/ESP32_WiFi_v3.x`
```

#### 3. Compile & upload

If necessary, put the ESP into bootloader mode. See [documentation on boot mode selection here](https://github.com/espressif/esptool/wiki/ESP32-Boot-Mode-Selection).

Some boards handle entering bootloader mode automatically. On other ESP boards, like the Adafruit HUZZAH 32 Breakout board, you have to press and hold a `boot` button then press a `reset` button, as [seen here](https://learn.adafruit.com/huzzah32-esp32-breakout-board/using-with-arduino-ide#blink-test-3-13).

Compile and upload using PlatformIO. To compile against the default env (for the ESP_WROVER_KIT, [docs](https://docs.platformio.org/en/latest/boards/espressif32/esp-wrover-kit.html)), run:

```shell
pio run -t upload
```

If you are using a different development board, you can specify one of the envs setup in `platformio.ini`, for example:

```shell
pio run -e openevse_huzzah32 -t upload
```

> **_NOTE:_** To enable OTA updates, first upload via serial using the dev environment. This enables OTA enable build flag*

---

## Troubleshooting

### Uploading issues

- Double check device is in bootloader mode
- Try reducing the upload ESP baudrate
- Erase flash: If you are experiencing ESP hanging in a reboot loop after upload it may be that the ESP flash has remnants of previous code (which may have the used the ESP memory in a different way). The ESP flash can be fully erased using [esptool](https://github.com/espressif/esptool). With the unit in bootloader mode run:

```shell
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

```shell
esptool.py write_flash 0x000000 blank_1MB.bin 0x100000 blank_1MB.bin 0x200000 blank_1MB.bin 0x300000 blank_1MB.bin
```

### View serial debug

To help debug, it may be useful to enable serial debug output. To do this upload using `openevse_dev` environment e.g.

```shell
pio run -t upload -eopenevse_dev
```

The default is to enable serial debug on serial1 the ESP's 2nd serial port. You will need to connect a debugger to the ESP serial1 Tx pin (GPIO2).

To change to use serial0 (the main ESP's serial port) change `-DDEBUG_PORT=Serial1` to `-DDEBUG_PORT=Serial` in `platformio.ini`. Note that using serial 0 will adversely effect RAPI communication with the openevse controller.

---

# About

Collaboration of [OpenEnegyMonitor](http://openenergymonitor.org) and [OpenEVSE](https://openevse.com).

Contributions by:

- @glynhudson
- @chris1howell
- @trystanlea
- @jeremypoulter
- @sandeen
- @lincomatic
- @joverbee

---

# Licence

GNU General Public License (GPL) V3
