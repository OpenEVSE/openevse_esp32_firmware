# OpenEVSE Wired Ethernet using ESP32-Gateway

Sometimes getting a WiFi connection to an OpenEVSE / EmonEVSE install location can be troublesome.

It's now possible to connect the OpenEVSE / EmonEVSE via wired Ethernet using an ESP32-Gateway module. This module is a drop in replacement for ESP8266 / ESP32 WiFi modules and is compatible with all models of OpenEVSE / EmonEVSE. 

See [OpenEnergyMonitor web-store to purchase a pre-wired ESP32-Gateway module](https://shop.openenergymonitor.com/openevse-etherent-gateway-esp32/). The Ethernet gateway in our web-store will come with pre-wired power supply, serial connections, and pre-loaded with firmware for drop in replacement. 

![esp32-gateway-prewired](esp32-gateway-prewired.jpg)


Network connection can be made with a standard Ethernet cable. For new installations it may be worth considering a power cable with integrated data connections such as the [Doncaster EV-ultra cable](http://www.doncastercables.com/cables/17/77/EV-Ultra/Power-and-data-connectivity-combined-in-one-cable/). If using such a cable, extra work will be required to attach RJ45 connector or socket at each end. 

## Hardware Connections 

*Note: The these hardware connections apply to the current Rev.E & Rev.F ESP32-gateway hardware revisions. See section below for older units.*


|Signal        | Pin No.   | EVSE connector |
| :---------- | :---------- | :------------------- |
5V             | pin 20        | Red wire |
GND            | pin 19        | Black wire | 
Tx GPIO 32     | pin 13       | Yellow wire |
Rx GPIO 16     | pin 11       | Green wire |

![esp32-gateway-connections](esp32-gateway-connections.jpg)

The ESP32-gateway can be installed in the EmonEVSE as follows:

![esp32-gateway-emonevse](esp32-gateway-emonevse.jpg)


## Firmware 

ESP32-gateway modules from the OpenEnergyMonitor store come pre-loaded with firmware. Updates to firmware can be made via the web interface. 

## Uploading pre-compiled

Pre-compiled FW can be downloaded from the [repo releases page](https://github.com/OpenEVSE/ESP32_WiFi_V3.x/releases/), look for `esp32-gateway-e.bin`

ESP32-gateway can be connected by micro USB and firmware can be uploaded using esptool:

`esptool esptool.py --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 bootloader.bin 0x8000  partitions.bin 0x10000  esp32-gateway-e.bin`


## Compile and Upload 

Firmware can be compiled and upload using PlatformIO with the `openevse_esp32-gateway-e` environment selected. The `e` environment has been tested to work on hardware Rev.E and Rev.F. See note below for older revisions. 

The ESP32-gateway can be connected via micro USB and firmware compiled and uploaded with
`$ pio run -e openevse_esp32-gateway-e -t upload`

## Operation 

The ESP32-gateway supports both WiFi and Ethernet, if an Ethernet cable is not connected the ESP32-gateway will broadcast a WiFi AP `OpenEVSE_xxx`, connect with Passkey `openevse`.

When an Ethernet cable is connected WiFi will be disabled and the local network IP address and hostname displayed on the LCD. 

Note: Static IP or custom gateway IP address settings are currently not supported. 

The web UI will notify that connection is via Wired Ethernet.

![esp32-gateway-connected](esp32-gateway-connected.png)
## Feedback

The ESP32-Gateway is a new addition and is currently considered in 'Beta' since the ESP32 firmware is still under active development. However, it has been extensively tested and proven reliable for many months of operation. Please report your experience to the [OpenEnergyMonitor Community Forums](https://community.openenergymonitor.org/).
***

## Older Hardware Revisions

This guide focuses on using ESP-gateway hardware rev.E and above. If using a hardware rev older than rev.E the pin connections and firmware is different: 

|Signal        | Pin No.   | EVSE connector |
| :---------- | :---------- | :------------------- |
5V             | pin 20        | Red wire |
GND            | pin 19        | Black wire | 
Tx GPIO 17     | pin 12       | Yellow wire |
Rx GPIO 16     | pin 11       | Green wire |

Firmware for older hardware revisions can be compiled and uploading using:

`$ pio run -e openevse_esp32-gateway -t upload`

See [this git issue](https://github.com/OpenEVSE/ESP32_WiFi_V3.x/issues/12) for discussion of hardware revision changes. 
