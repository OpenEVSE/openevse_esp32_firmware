# OpenEVSE WiFi Gateway

[![Build Status](https://travis-ci.org/OpenEVSE/ESP32_WiFi_V3.x.svg?branch=master)](https://travis-ci.org/OpenEVSE/ESP32_WiFi_V3.x)

![main](docs/main2.png)

The WiFi gateway uses an **ESP32** which communicates with the OpenEVSE controller via serial utilizing the existing RAPI API serial interface. The web interface UI is served directly from the ESP32 web server and can be controlled via a connected device over the network.

[**See this repo for the older V2.x ESP8266 version**](https://github.com/openevse/ESP8266_WiFi_v2.x/)

Live demo: https://openevse.openenergymonitor.org

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

### OpenEVSE charging station
  - Purchase via: [OpenEVSE Store (USA/Canda)](https://store.openevse.com) | [OpenEnergyMonitor (UK / EU)](https://shop.openenergymonitor.com/openevse-deluxe-ev-charge-controller-kit/)
  - OpenEVSE FW [V4.8.0+ recommended](https://github.com/OpenEVSE/open_evse/releases)
  - All new OpenEVSE units are shipped with V4.8.0 pre-loaded (October 2017 onwards)
  - OpenEVSE FW V3.10.4 will work with latest WiFi FW with some minor issues e.g. LCD text corruption

### WiFi Module

- ESP32
- Purchase via: [OpenEVSE Store (USA/Canda)](https://store.openevse.com/collections/frontpage/products/openevse-wifi-kit) | [OpenEnergyMonitor (UK / EU)](https://shop.openenergymonitor.com/evse/)
- See [OpenEVSE WiFi setup guide](https://openevse.dozuki.com/Guide/OpenEVSE+WiFi+%28Beta%29/14) for WiFi module hardware connection instructions

***

## Contents

<!-- toc -->

- [User Guide](#user-guide)
  * [WiFi Setup](#wifi-setup)
  * [OpenEVSE Web Interface](#openevse-web-interface)
  * [Charging Mode: Eco](#charging-mode-eco)
    + [Solar PV Divert Example](#solar-pv-divert-example)
    + [Setup](#setup)
    + [Operation](#operation)
  * [Services](#services)
    + [Emoncms data logging](#emoncms-data-logging)
    + [MQTT](#mqtt)
      - [OpenEVSE Status via MQTT](#openevse-status-via-mqtt)
    + [RAPI](#rapi)
      - [RAPI via web interface](#rapi-via-web-interface)
      - [RAPI over MQTT](#rapi-over-mqtt)
      - [RAPI over HTTP](#rapi-over-http)
    + [OhmConnect](#ohmconnect)
  * [System](#system)
    + [Authentication](#authentication)
    + [Hardware reset](#hardware-reset)
    + [Firmware update](#firmware-update)
- [Development Guide](#development-guide)
  * [Compiling and uploading firmware](#compiling-and-uploading-firmware)
    + [Building GUI static assets](#building-the-gui-static-assets)
    + [Compile and upload using PlatformIO](#compile-and-upload-using-platformio)
      - [1. Install PlatformIO](#1-install-platformio)
      - [2. Clone this repo](#2-clone-this-repo)
      - [3. Compile & upload](#3-compile--upload)
  * [Troubleshooting](#troubleshooting)
    + [Uploading issues](#uploading-issues)
    + [Fully erase ESP](#fully-erase-esp)
    + [View serial debug](#view-serial-debug)
- [About](#about)
- [Licence](#licence)

<!-- tocstop -->

***

# User Guide

## WiFi Setup

On first boot, OpenEVSE should broadcast a WiFI access point (AP) `OpenEVSE_XXX`. Connect to this AP (default password: `openevse`) and the [captive portal](https://en.wikipedia.org/wiki/Captive_portal) should forward you to the log-in page. If this does not happen navigate to [http://openevse](http://openevse), [http://openevse.local](http://openevse.local) or [http://192.168.4.1](http://192.168.4.1)

*Note: You may need to disable mobile data if connecting via a mobile*

*Note: Use of Internet Explorer 11 or earlier is not recommended*

![Wifi connect](docs/wifi-connect.png) ![Wifi setup](docs/wifi-scan.png)


- Select your WiFi network from list of available networks
- Enter WiFi PSK key then click `Connect`

- OpenEVSE should now connect to local WiFi network
- Re-connect device to local WiFi network and connect to OpenEVSE using [http://openevse.local](http://openevse.local), [http://openevse](http://openevse) or local IP address.

**If connection / re-connection fails (e.g. network cannot be found or password is incorrect) the OpenEVSE will automatically revert back to WiFi access point (AP) mode after a short while to allow a new network to be re-configured if required. Re-connection to existing network will be attempted every 5 minutes.**

*Holding the `boot / GPIO0` button on the ESP8266 module for about 5s will force WiFi access point mode. This is useful when trying to connect the unit to a new WiFi network. If the unit cannot connect t0 a WiFi network it will resturn to AP more before retrying to connect*

***

## OpenEVSE Web Interface

All functions of the OpenEVSE can be viewed and controlled via the web interface. Here is a screen grab showing the 'advanced' display mode:

![advanced](docs/adv.png)

The interface has been optimised to work well for both desktop and mobile. Here is an example setting a charging delay timer using an Android device:

![android-clock](docs/mobile-clock.png)

## Charging Mode: Eco

'Eco' charge mode allows the OpenEVSE to adjust the charging current automatically based on an MQTT feed. This feed could be the amount of solar PV generation or the amount of excess power (grid export). 'Normal' charge mode charges the EV at the maximum rate set.

### Solar PV Divert Example

This is best illustrated using an Emoncms MySolar graph. The solar generation is shown in yellow and OpenEVSE power consumption in blue:

![divert](docs/divert.png)

- OpenEVSE is initially sleeping with an EV connected
- Once solar PV generation reaches 6A (1.5kW @ 240V) the OpenEVSE initiates charging
- Charging current is adjusted based on available solar PV generation
- Once charging has begun, even if generation drops below 6A, the EV will continue to charge*

**The decision was made not to pause charging if generation current drops below 6A since repeatedly starting / stopping a charge causes excess wear to the OpenEVSE relay contactor.*

If a Grid +I/-E (positive import / negative export) feed was used the OpenEVSE would adjust its charging rate based on *excess* power that would be exported to the grid; for example, if solar PV was producing 4kW and 1kW was being used on-site, the OpenEVSE would charge at 3kW and the amount exported to the grid would be 0kW. If on-site consumption increases to 2kW the OpenEVSE would reduce its charging rate to 2kW.

An [OpenEnergyMonitor solar PV energy monitor](https://guide.openenergymonitor.org/applications/solar-pv/) with an AC-AC voltage sensor adaptor is required to monitor direction of current flow.

### Setup

![eco](docs/eco.png)

- To use 'Eco' charging mode MQTT must be enabled and 'Solar PV divert' MQTT topics must be entered.
- Integration with an OpenEnergyMonitor emonPi is straightforward:
  - Connect to emonPi MQTT server, [emonPi MQTT credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt) should be pre-populated
  - Enter solar PV generation / Grid (+I/-E) MQTT topic e.g. if solar PV is being monitored by emonPi CT channel 1 enter `emon/emonpi/power1`
  - [MQTT lens Chrome extension](https://chrome.google.com/webstore/detail/mqttlens/hemojaaeigabkbcookmlgmdigohjobjm?hl=en) or [MQTT Explorer](http://mqtt-explorer.com/) tools can be used to view MQTT data e.g. subscribe to `emon/#` for all OpenEnergyMonitor MQTT data. To lean more about MQTT see [MQTT section of OpenEnergyMonitor user guide](https://guide.openenergymonitor.org/technical/mqtt/).
  - If using Grid +I/-E (positive import / negative export) MQTT feed ensure the notation positive import / negative export is correct, CT sensor can be physically reversed on the cable to invert the reading.

### Operation

To enable 'Eco' mode charging:

- Connect EV and ensure EV's internal charging timer is switched off
- Pause charge; OpenEVSE should display 'sleeping'
- Enable 'Eco' mode using web interface or via MQTT
- EV will not begin charging when generation / excess current reaches 6A (1.4kW @ 240V)

- During 'Eco' charging changes to charging current are temporary (not saved to EEPROM)
- After an 'Eco mode' charge the OpenEVSE will revert to 'Normal' when EV is disconnected and previous 'Normal' charging current will be reinstated.
- Current is adjusted in 1A increments between 6A* (1.5kW @ 240V) > max charging current (as set in OpenEVSE setup)
- 6A is the lowest supported charging current that SAE J1772 EV charging protocol supports
- The OpenEVSE does not adjust the current itself but rather request that the EV adjusts its charging current by varying the duty cycle of the pilot signal, see [theory of operation](https://openev.freshdesk.com/support/solutions/articles/6000052070-theory-of-operation) and [Basics of SAE J1772](https://openev.freshdesk.com/support/solutions/articles/6000052074-basics-of-sae-j1772).
- Charging mode can be viewed and set via MQTT: `{base-topic}/divertmode/set` (1 = normal, 2 = eco).

\* *OpenEVSE controller firmware [V4.8.0](https://github.com/OpenEVSE/open_evse/releases/tag/v4.8.0) has a bug which restricts the lowest charging current to 10A. The J1772 protocol can go down to 6A. This ~~will~~ has be fixed with a firmware update. See [OpenEnergyMonitor OpenEVSE FW releases](https://github.com/openenergymonitor/open_evse/releases/). A ISP programmer is required to update openevse controler FW.*

***

## Services

![services](docs/services.png)

### Emoncms data logging

OpenEVSE can post its status values (e.g amp, temp1, temp2, temp3, pilot, status) to [emoncms.org](https://emoncms.org) or any other  Emoncms server (e.g. emonPi) using [Emoncms API](https://emoncms.org/site/api#input). Data will be posted every 30s.

Data can be posted using HTTP or HTTPS. For HTTPS the Emoncms server must support HTTPS (emoncms.org does, the emonPi does not).Due to the limited resources on the ESP the SSL SHA-1 fingerprint for the Emoncms server must be manually entered and regularly updated.

*Note: the emoncms.org fingerprint will change every 90 days when the SSL certificate is renewed.*


### MQTT

MQTT and MQTTS (secure) connections are supported for status and control. 

At startup the following message is published with a retain flag to `openevse/announce/xxx` where `xxx` is the last 4 characters of the device ID. This message is useful for device discovery and contans the device hostname and IP address. 
```
{"state":"connected","id":"c44f330dxxad","name":"openevse-55ad","mqtt":"emon/openevse-55ad","http":"http://192.168.1.43/"}
```

For device descovery you should subscribe with a wild card to `openevse/announce/#`

When the device disconnects from MQTT the same message is posted with `state":"disconnected"` (Last Will and Testament).

All subsequent MQTT status updates will by default be be posted to `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID. This base-topic can be changed via the MQTT service page. 

#### OpenEVSE Status via MQTT

OpenEVSE can post its status values (e.g. amp, wh, temp1, temp2, temp3, pilot, status) to an MQTT server. Data will be published as a sub-topic of base topic.E.g `<base-topic>/amp`. Data is published to MQTT every 30s.

**The default `<base-topic>` is `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID**

MQTT setup is pre-populated with OpenEnergyMonitor [emonPi default MQTT server credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt).

- Enter MQTT server host and base-topic
- (Optional) Enter server authentication details if required
- Click connect
- After a few seconds `Connected: No` should change to `Connected: Yes` if connection is successful. Re-connection will be attempted every 10s. A refresh of the page may be needed.

*Note: `emon/xxxx` should be used as the base-topic if posting to emonPi MQTT server if you want the data to appear in emonPi Emoncms. See [emonPi MQTT docs](https://guide.openenergymonitor.org/technical/mqtt/).*

MQTT can also be used to control the OpenEVSE, see RAPI MQTT below.

### RAPI

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


### HTTP API 

Current status of the OpenEVSE in JSON format is available via: `http://openevse-xxx/status` e.g

```
{"mode":"STA","wifi_client_connected":1,"eth_connected":0,"net_connected":1,"srssi":-73,"ipaddress":"192.168.1.43","emoncms_connected":1,"packets_sent":22307,"packets_success":22290,"mqtt_connected":1,"ohm_hour":"NotConnected","free_heap":203268,"comm_sent":335139,"comm_success":335139,"rapi_connected":1,"amp":0,"pilot":32,"temp1":282,"temp2":-2560,"temp3":-2560,"state":254,"elapsed":3473,"wattsec":22493407,"watthour":51536,"gfcicount":0,"nogndcount":0,"stuckcount":0,"divertmode":1,"solar":390,"grid_ie":0,"charge_rate":7,"divert_update":0,"ota_update":0,"time":"2020-05-12T17:53:48Z","offset":"+0000"}
``` 

Current config of the OpenEVSE in JSON format is available via `http://openevse-xxx/config` e.g

```
{"firmware":"6.2.1.EU","protocol":"5.1.0","espflash":4194304,"version":"3.1.0.dev","diodet":0,"gfcit":0,"groundt":0,"relayt":0,"ventt":0,"tempt":0,"service":2,"scale":220,"offset":0,"ssid":"<SSID>","pass":"_DUMMY_PASSWORD","emoncms_enabled":true,"emoncms_server":"https://emoncms.org","emoncms_node":"emonevse","emoncms_apikey":"_DUMMY_PASSWORD","emoncms_fingerprint":"","mqtt_enabled":true,"mqtt_protocol":"mqtt","mqtt_server":"emonpi","mqtt_port":1883,"mqtt_reject_unauthorized":true,"mqtt_topic":"emon/openevse-55ad","mqtt_user":"emonpi","mqtt_pass":"_DUMMY_PASSWORD","mqtt_solar":"emon/solarpv/test","mqtt_grid_ie":"","mqtt_supported_protocols":["mqtt","mqtts"],"http_supported_protocols":["http","https"],"www_username":"open","www_password":"_DUMMY_PASSWORD","hostname":"openevse-55ad","time_zone":"Europe/Lisbon|WET0WEST,M3.5.0/1,M10.5.0","sntp_enabled":true,"sntp_host":"pool.ntp.org","ohm_enabled":false}
```

### HTTP Tesla API

**IN DEVELOPMENT**

V3.2 onwards includes a basic Tesla API integration. The HTTP API for this is as follows:

Enter Tesla credentials and enable the feature: 

`http://openevse-xxx/savetesla?user=TESLAUSER&pass=TESLAPASS&enable=true`

Return a list of vehicles associated with the Tesla account e.g

`http://openevse-xxx/teslaveh`

e.g
`
{"count:"2,[{"id":"xxxx","name":"tesla1"},{"id":"xxxxx","name":"tesla2"}]}`

Set the vehicle index to choose which vehicle to retrieve status.
Note: The ID starts at zero so the first car will have vi=0

`http://openevse-xxx/saveteslavi?vi=VEHICLEINDEX`

The SoC and rated range of the Tesa vehicle is now displayed in JSON format via `/status`. 

#### RAPI over HTTP

RAPI (rapid API) commands can also be issued directly via a single HTTP request.

*Assuming `192.168.0.108` is the local IP address of the OpenEVSE ESP.*

Eg.the RAPI command to set charging rate to 13A:

[http://192.168.0.108/r?rapi=%24SC+13](http://192.168.0.108/r?rapi=%24SC+13)

To sleep (pause a charge) issue RAPI command `$FS`

[http://192.168.0.108/r?rapi=%24FS](http://192.168.0.108/r?rapi=%24FS)

To enable (start / resume a charge) issue RAPI command `$FE`

[http://192.168.0.108/r?rapi=%24FE](http://192.168.0.108/r?rapi=%24FE)


There is also an [OpenEVSE RAPI command python library](https://github.com/tiramiseb/python-openevse).

### OhmConnect

**USA California only**
[Join here](https://ohm.co/openevse)

**Video - How does it Work**
https://player.vimeo.com/video/119419875

-Sign Up
-Enter Ohm Key

Ohm Key can be obtained by logging in to OhmConnect, enter Settings and locate the link in "Open Source Projects"
Example: https://login.ohmconnect.com/verify-ohm-hour/OpnEoVse
Key: OpnEoVse

## System

![system](docs/system.png)

### Authentication

Admin HTTP Authentication (highly recommended) can be enabled by saving admin config by default username and password.

**HTTP authentication is required for all HTTP requests including input API**


### Hardware reset

A Hardware reset can be made (all WiFi and services config lost) by pressing and holding GPIO0 hardware button (on the Huzzah WiFi module) for 10s.

*Note: Holding the GPIO0 button for 5s will but the WiFi unit into AP (access point) mode to allow the WiFi network to be changed without loosing all the service config*

### Firmware update

See [OpenEVSE Wifi releases](https://github.com/OpenEVSE/ESP32_WiFi_v3.x/releases) for latest stable pre-compiled update releases.

#### Via Web Interface

This is the easiest way to update. Pre-compiled firmware `.bin` files can be uploaded via the web interface: System > Update.

If for whatever reason the web-interface won't load it's possible to update the firmware via CURL:

`curl -F 'file=@firmware.bin'  http://<IP-ADDRESS>/update && echo`

#### Via Network OTA

The firmware can also be updated via OTA over a local WiFi network using PlatformIO:

`platformio run -t upload --upload-port <IP-ADDRESS>`

#### Via USB Serial Programmer

On the command line using the [esptool.py utility](https://github.com/espressif/esptool):

If flashing a new ESP32 flashing bootloader and partitions file is required: 

`esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin`

Then succesive uploads can just upload the firmware 

`esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x10000 firmware.bin`

Or with the [NodeMCU PyFlasher](https://github.com/marcelstoer/nodemcu-pyflasher) GUI, available with pre-built executable files for windows/mac.

# Development guide

## Compiling and uploading firmware

It is necessary to download and build the static web assets for the GUI before compiling and uploading to the ESP.

### Building the GUI static assets

The GUI static web assets are minified and compiled into the firmware using a combination of Webpack and a [custom python build script](scripts/extra_script.py).

You will need Node.js and npm installed: https://nodejs.org/en/download/

In addition, the GUI is now maintained in a [separate repository](https://nodejs.org/en/download/package-manager/) and is included as a Git submodule.

If the `gui` directory is empty, use the following to retrieve the GUI source and fetch the dependencies:

```shell
git submodule update --init
cd gui
npm install
```

To 'build' the GUI static assets, run the following from the `gui` directory:

```shell
npm run build
```

Now you are ready to compile and upload to the ESP32.

### Compile and upload using PlatformIO

For info on the Arduino core for the ESP32 using PlatformIO, see: https://github.com/espressif/arduino-esp32/blob/master/docs/platformio.md

#### 1. Install PlatformIO

PlatformIO can be installed as a command line utility (PlatformIO Core), a standalone IDE (PlatformIO IDO) or as an integration into other popular IDEs. Follow the [installation instructions](https://platformio.org/install).

#### 2. Clone this repo

`$ git clone https://github.com/OpenEVSE/ESP32_WiFi_v3.x`

#### 3. Compile & upload

- If necessary, put the ESP into bootloader mode. See [documentation on boot mode selection here](https://github.com/espressif/esptool/wiki/ESP32-Boot-Mode-Selection).
  - Some boards handle entering bootloader mode automatically. On other ESP boards, like the Adafruit HUZZAH 32 Breakout board, you have to press and hold a `boot` button then press a `reset` button, as [seen here](https://learn.adafruit.com/huzzah32-esp32-breakout-board/using-with-arduino-ide#blink-test-3-13).

Compile and upload using PlatformIO. To compile against the default env (for the ESP_WROVER_KIT, [docs](https://docs.platformio.org/en/latest/boards/espressif32/esp-wrover-kit.html)), run:

```
pio run -t upload
```

If you are using a different development board, you can specify one of the envs setup in `platformio.ini`, for example:

```
pio run -e openevse_huzzah32 -t upload
```

*To enable OTA updates, first upload via serial using the dev environment. This enables OTA enable build flag*

***

## Troubleshooting

### Uploading issues

- Double check device is in bootloder mode
- Try reducing the upload ESP baudrate
- Erase flash: If you are experiencing ESP hanging in a reboot loop after upload it may be that the ESP flash has remnants of previous code (which may have the used the ESP memory in a different way). The ESP flash can be fully erased using [esptool](https://github.com/espressif/esptool). With the unit in bootloader mode run:

`$ esptool.py erase_flash`

Output:

```
esptool.py v1.2-dev
Connecting...
Running Cesanta flasher stub...
Erasing flash (this may take a while)...
Erase took 8.0 seconds
```

### Fully erase ESP

To fully erase all memory locations on an ESP-12 (4Mb) we need to upload a blank file to each memory location

`esptool.py write_flash 0x000000 blank_1MB.bin 0x100000 blank_1MB.bin 0x200000 blank_1MB.bin 0x300000 blank_1MB.bin`

### View serial debug

To help debug it may be useful to enable serial debug output. To do this upload using `openevse_dev` environment e.g.

`pio run -t upload -eopenevse_dev`

The default is to enable serial debug on serial1 the ESP's 2nd serial port. You will need to connect a debugger to the ESP serial1 Tx pin (GPIO2).

To change to use serial0 (the main ESP's serial port) change `-DDEBUG_PORT=Serial1` to `-DDEBUG_PORT=Serial` in `platformio.ini`. Note that using serial 0 will adversely effect RAPI communication with the openevse controller.

***

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

# Licence

GNU General Public License (GPL) V3
