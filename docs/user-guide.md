# OpenEVSE User Guide

## Contents

<!-- toc -->

* [Hardware](#hardware)
* [WiFi Setup](#wifi-setup)
* [Charging Mode: Eco](#charging-mode--eco)
  + [Eco Mode Setup](#eco-mode-setup)
  + [Eco Mode Advanced Settings](#eco-mode-advanced-settings)
* [Services](#services)
  + [Emoncms data logging](#emoncms-data-logging)
  + [MQTT](#mqtt)
    - [OpenEVSE Status via MQTT](#openevse-status-via-mqtt)
  + [OhmConnect](#ohmconnect)
* [System](#system)
  + [Authentication](#authentication)
  + [WiFi Reset](#wifi-reset)
  + [HTTP Auth Password reset](#http-auth-password-reset)
  + [Hardware Factory Reset](#hardware-factory-reset)
* [Firmware update](#firmware-update)
  + [Via Web Interface](#via-web-interface)
  + [Via Network OTA](#via-network-ota)
  + [Via USB Serial Programmer](#via-usb-serial-programmer)

<!-- tocstop -->

## Hardware

Most ESP32 boards can be used (see platfromio.ini for full list of supported boards), however the boards which is best supported is the OpenEVSE WiFi V1. 

**Be sure to correctly identify your WiFi hardware before updating the firmware**

![Wifi Modules](openevse-wifi-modules.png) 

- Huzzah ESP8266 - can only run V2.x firmware, see [archive V2.x repository](https://github.com/OpenEVSE/ESP8266_WiFi_v2.x)
- Huzzah ESP32 - can run V3.x and V4.x firmware
- OpenEVSE V1 - designed for V4.x firmware
- [Olimex ESP32 Gateway (Wired Ethernet)](wired-ethernet.md) - can run V3.x and V4.x firmware

## WiFi Setup

On first boot, OpenEVSE should broadcast a WiFi access point (AP) `OpenEVSE_XXXX`. Connect your browser device to this AP (default password: `openevse`) and the [captive portal](https://en.wikipedia.org/wiki/Captive_portal) should forward you to the log-in page. If this does not happen, navigate to [http://openevse](http://openevse), [http://openevse.local](http://openevse.local) or [http://192.168.4.1](http://192.168.4.1)

*Note: You may need to disable mobile data if connecting via a mobile*

![Wifi connect](wifi-connect.png)

![Wifi setup](wifi-scan.png)

- Select your WiFi network from list of available networks
- Enter WiFi Passkey, then click `Connect`

- OpenEVSE should now connect to local WiFi network
- Re-connect your browsing device to local WiFi network and connect to OpenEVSE using [http://openevse.local](http://openevse.local), [http://openevse](http://openevse) or local IP address.

**If connection / re-connection fails (e.g. network cannot be found or password is incorrect) the OpenEVSE will automatically revert back to WiFi access point (AP) mode after a short while to allow a new network to be re-configured if required. Re-connection to existing network will be attempted every 5 minutes.**

*Holding the `boot / GPIO0` button on the ESP8266 module for about 5s will force WiFi access point mode. This is useful when trying to connect the unit to a new WiFi network. If the unit cannot connect to a WiFi network it will return to AP mode before retrying to connect*

***

## Charging Mode: Eco

'Eco' charge mode allows the OpenEVSE to start/stop and adjust the charging current automatically based on an MQTT. This feed could be the amount of solar PV generation or the amount of excess power (grid export). 'Normal' charge mode charges the EV at the maximum rate set.

![eco](eco.png)

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

**Note#1: 'Grid' feed should include the power consumed by the EVSE**
**Note#2: The EVSE expects the MQTT data to update every 5-10s, perforamce will be degraded if the update interval is much faster or slower than this**

**CT sensor can be physically reversed on the cable to invert the reading.*

[MQTT Explorer](http://mqtt-explorer.com/) can be used to view MQTT data. To learn more about MQTT see [MQTT section of OpenEnergyMonitor user guide](https://guide.openenergymonitor.org/technical/mqtt/).

Divertmode can be controlled via mqtt

Topic: `<base-topic>/divertmode/set` 
Value: `1` = Normal or `2` = Eco

### Eco Mode Advanced Settings 

If 'advanced' mode is toggled on the UI more solar PV divert settings will become available: 

![eco](divert-advanced.png)

- Required PV power ratio: specifies which fraction of the EV charging current should come from PV excess. Default value 110% (1.1)
- Divert smoothing attack: controls how quickly the EVSE responds to an increase in solar PV / grid excess. Default value 40% (0.4)
- Divert Smoothing decay: controls how quickly the EVSE responds to a decrease in solar PV / grid excess. Default value 5% (0.05)
- Minimum charge time: the amount of time in seconds the EVSE should run for when triggered by solar PV / grid excess

See this [interactive spreadsheet](https://docs.google.com/spreadsheets/d/1GQEAQ5QNvNuShEsUdcrNsFC12U3pQfcD_NetoIfDoko/edit?usp=sharing) to explore how these values effect the smoothing algorithm.

**Caution: adjust these values at your own risk, the default values have been set to minimise wear on the EVSE contactor and the EVs chraging system. Rapid switching of the EVSE will result in increased wear on these components**


***

## Services

![services](services.png)

### Emoncms data logging

OpenEVSE can post its status values to [emoncms.org](https://emoncms.org) or any other  Emoncms server (e.g. emonPi) using [Emoncms API](https://emoncms.org/site/api#input). Data will be posted every 30s.

Data can be posted using HTTP or HTTPS.


### MQTT

MQTT and MQTTS (secure) connections are supported for status and control. 

At startup the following message is published with a retain flag to `openevse/announce/xxxx` where `xxxx` is the last 4 characters of the device ID. This message is useful for device discovery and contans the device hostname and IP address. 

```json
{
  "state":"connected",
  "id":"c44f330dxxad",
  "name":"openevse-55ad",
  "mqtt":"emon/openevse-55ad",
  "http":"http://192.168.1.43/"
}
```

For device discovery you should subscribe with a wild card to `openevse/announce/#`

When the device disconnects from MQTT the same message is posted with `state":"disconnected"` (Last Will and Testament).

All subsequent MQTT status updates will by default be be posted to `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID. This base-topic can be changed via the MQTT service page. 

#### OpenEVSE Status via MQTT

OpenEVSE can post its status values (e.g. amp, wh, temp1, temp2, temp3, pilot, status) to an MQTT server. Data will be published as a sub-topic of base topic, e.g `<base-topic>/amp`. Data is published to MQTT every 30s.

**The default `<base-topic>` is `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID**

MQTT setup is pre-populated with OpenEnergyMonitor [emonPi default MQTT server credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt).

- Enter MQTT server host and base-topic
- (Optional) Enter server authentication details if required
- Click connect
- After a few seconds `Connected: No` should change to `Connected: Yes` if connection is successful. Re-connection will be attempted every 10s. A refresh of the page may be needed.

*Note: `emon/xxxx` should be used as the base-topic if posting to emonPi MQTT server if you want the data to appear in emonPi Emoncms. See [emonPi MQTT docs](https://guide.openenergymonitor.org/technical/mqtt/).*

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

![system](system.png)

### Authentication

Admin HTTP Authentication (highly recommended) can be enabled by saving admin config by default username and password.

**HTTP authentication is required for all HTTP requests including input API**

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

*Note: Holding the GPIO0 button for 5s will put the WiFi unit into AP (access point) mode to allow the WiFi network to be changed without losing all the service config*

## Firmware update

Firmware can be updated via the Web UI
See [OpenEVSE Wifi releases](https://github.com/OpenEVSE/ESP32_WiFi_v3.x/releases) for latest stable pre-compiled update releases.

### Via Web Interface

This is the easiest way to update. Pre-compiled firmware `.bin` files can be uploaded via the web interface: System > Update.

If for whatever reason the web-interface won't load it's possible to update the firmware via cURL:

```bash
curl -F 'file=@firmware.bin'  http://<IP-ADDRESS>/update && echo
```

### Via Network OTA

The firmware can also be updated via OTA over a local WiFi network using PlatformIO:

```bash
platformio run -t upload --upload-port <IP-ADDRESS>
```

### Via USB Serial Programmer

[Compatiable USB to Serial Programmer](https://shop.openenergymonitor.com/programmer-usb-to-serial-uart/)

On the command line using the [esptool.py utility](https://github.com/espressif/esptool):

If flashing a new ESP32, flashing bootloader and partitions file is required: 

```bash
esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

Then successive uploads can just upload the firmware 

```bash
esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x10000 firmware.bin
```

**If uploading to ESP32 Etherent gateway, use slower baudrate of `115200`**

Or with the [NodeMCU PyFlasher](https://github.com/marcelstoer/nodemcu-pyflasher) GUI, available with pre-built executable files for Windows/Mac.


