# OpenEVSE WiFi

OpenEVSE ESP8266 WIFI serial to EmonCMS link

![OpenEVSE_WiFi.jpg](docs/OpenEVSE_WiFi.jpg)

***

## Requirements

- ESP-12E module with 4M Flash

***

# OpenEVSE User Guide

## First Setup

On first boot, OpenEVSE should broadcast a WiFI AP `OpenEVSE_XXX`. Connect to this AP (default password: `openevse`) and the [captive portal](https://en.wikipedia.org/wiki/Captive_portal) should forward you to the log-in page. If this does not happen navigate to `http://192.168.4.1`

*Note: You may need to disable mobile data if connecting via a Android 6 device*

![Wifi connect](docs/wifi-connect.png)

### 1. WiFi Connection


- Select your WiFi network from list of available networks
- Enter WiFi PSK key then click `Connect`

![Wifi setup](docs/wifi-scan.png)

- OpenEVSE should now connect to local wifi network and return local IP address.
- Browse to local IP address by clicking the hyperlink (assuming your computer is on the same WiFi network)
On future boot up OpenEVSE will automatically connect to this network.

*Note: on some networks it's possible to browse to the OpenEVSE using hostname [http://openevse](http://openevse) or on windows [http://openevse.local](http://openevse.local)*

**If re-connection fails (e.g. network cannot be found) the OpenEVSE will automatically revert back to WiFi AP mode after a short while to allow a new network to be re-configued if required. Re-connection to existing network will be attempted every 5min.**

*Holding the `boot` button at startup (for about 10's) will force AP mode. This is useful when trying to connect the unit to a new WiFi network.*


## 2. Emoncms

![emoncms setup](docs/emoncms.png)

OpenEVSE can post its status values (e.g amp, temp1, temp2, temp3, pilot, status) to [emoncms.org](https://emoncms.org) or any other  Emoncms server (e.g. emonPi) using [Emoncms API](https://emoncms.org/site/api#input). Data will be posted every 30s.

Data ca be posted using HTTP or HTTPS. For HTTPS the Emoncms server must support HTTPS (emoncms.org does, emonPi does not).Due to the limited resources on the ESP the SSL SSH-1 fingerprint for the Emoncms server must be manually entered and regularly updated.

*Note: the emoncms.org fingerprint will change every 90 days when the SSL certificate is renewed.*

**Currently emoncms.org only supports numerical node names, other emoncms servers e.g. emonPi do support alphanumeric node naming.**


## 3. MQTT

### OpenEVSE Status via MQTT

![mqtt setup](docs/mqtt.png)

OpenEVSE can post its status values (e.g. amp, temp1, temp2, temp3, pilot, status) to an MQTT server. Data will be published as a sub-topic of base topic.E.g `<base-topic>/amp`. Data is published to MQTT every 30s.

- Enter MQTT server host and base-topic
- (Optional) Enter server authentication details if required
- Click connect
- After a few seconds `Connected: No` should change to `Connected: Yes` if connection is successful. Re-connection will be attempted every 10s. A refresh of the page may be needed.

*Note: `emon/xxxx` should be used as the base-topic if posting to emonPi MQTT server if you want the data to appear in emonPi Emoncms. See [emonPi MQTT docs](https://guide.openenergymonitor.org/technical/mqtt/).*

### RAPI over MQTT

RAPI commands can be issued via MQTT messages. The RAPI command should be published to the following MQTT:

`<base-topic>/rapi/in/<$ rapi-command> payload`

e.g assuming base-topic of `openevse` to following command will set current to 13A:

`openevse/rapi/in/$SC 13`

The payload can be left blankc if the RAPI command does not require a payload e.g.

`openevse/rapi/in/$GC`

The responce from the RAPI command is published by the OpenEVSE back to the same sub-topic and can be received by subscribing to:

`<base-topic>/rapi/out/#`

e.g. `$OK`


## 5. Admin (Authentication)

HTTP Authentication (highly recomended) can be enabled by saving admin config by default username and password

**HTTP authentication is required for all HTTP requests including input API**

![admin setup](docs/admin.png)

## RAPI Commands over HTTP

RAPI (rapid API) commands can also be issued directly via a single HTTP request.

*Assuming `192.168.0.108` is the local IP address of the OpenEVSE ESP.*

Eg.the RAPI command to set charging rate to 13A:

[http://192.168.0.108/r?rapi=%24SC+13](http://192.168.0.108/r?rapi=%24SC+13)

To sleep (pause a charge) issue RAPI command `$FS`

[http://192.168.0.108/r?rapi=%24FS](http://192.168.0.108/r?rapi=%24FS)

To enable (start / resume a charge) issue RAPI command `$FE`

[http://192.168.0.108/r?rapi=%24FE](http://192.168.0.108/r?rapi=%24FE)


There is also an [OpenEVSE RAPI command python library](https://github.com/tiramiseb/python-openevse).

RAPI commands can be used to control and check the status of all OpenEVSE functions. A full list of RAPI commands can be found in the [OpenEVSE plus source code](https://github.com/lincomatic/open_evse/blob/stable/rapi_proc.h).

***

## Firmware Compile & Upload Instructions

The code for the ESP8266 can be compiled and uploaded using PlatformIO or Arduino IDE. PlatformIO is the easiest to get started.

### Using PlatformIO

For more detailed ESP8266 Arduino core specific PlatfomIO notes see: https://github.com/esp8266/Arduino#using-platformio

#### a. Install PlatformIO command line

The easiest way if running Linux is to install use the install script, this installed pio via python pip and installs pip if not present. See [PlatformIO installation docs](http://docs.platformio.org/en/latest/installation.html#installer-script). Or PlatformIO IDE can be used :

`$ sudo python -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio/master/scripts/get-platformio.py)"`

#### b. And / Or use PlatformIO IDE

Standalone built on GitHub Atom IDE, or use PlatformIO Atom IDE plug-in if you already have Atom installed. The IDE is nice, easy and self-explanitory.

[Download PlatfomIO IDE](http://platformio.org/platformio-ide)

#### 2. Clone this repo

`$ git clone https://github.com/chris1howell/OpenEVSE_RAPI_WiFi_ESP8266`


#### 3. Compile

```
$ cd OpenEVSE_RAPI_WiFi_ESP8266
$ pio run
```

The first time platformIO is ran the espressif arduino tool chain and all the required libs will be installed if required.

#### 3. Upload

- Put ESP into bootloader mode
   - On other ESP boards (Adafruit HUZZAH) press and hold `GPIO0` button then press Reset, LED should light dimly to indicate bootloader mode

##### a.) Compile & Upload main program:

`$ pio run -t upload`

##### b.) Upload data folder to the file system (html, CSS etc.) (SPIFFS):

- Put ESP back into bootloder mode, see above

`$ pio run -t uploadfs`

See [PlatfomIO docs regarding SPIFFS uploading](http://docs.platformio.org/en/latest/platforms/espressif.html#uploading-files-to-file-system-spiffs)

##### c.) OTA upload over local network (optional advanced)

`$  pio run  -t upload --upload-port <LOCAL-ESP-IP-ADDRESS>`

Upload SPIFFS filesystem over OTA (and don't flash):

` pio run -e emonesp_spiffs -t upload --upload-port <LOCAL-ESP-IP-ADDRESS>`

OTA uses port 8266. See [PlatformIO ESP OTA docs](http://docs.platformio.org/en/latest/platforms/espressif.html#over-the-air-ota-update):

***

### Using Arduino IDE

#### 1. Install ESP for Arduino with Boards Manager

From: https://github.com/esp8266/Arduino

Starting with 1.6.4, Arduino allows installation of third-party platform packages using Boards Manager. ESP Arduino packages are available for Windows, Mac OS, and Linux (32 and 64 bit).

- Install Arduino 1.6.8 from the Arduino website.
- Start Arduino and open Preferences window.
- Enter http://arduino.esp8266.com/stable/package_esp8266com_index.json into Additional Board Manager URLs field. You can add multiple URLs, separating them with commas.
- Open Boards Manager from Tools > Board menu and install esp8266 platform (and don't forget to select your ESP8266 board from Tools > Board menu after installation).

#### 2. Install ESP filesystem file uploader

From: https://github.com/esp8266/Arduino/blob/master/doc/filesystem.md

- Download the tool: https://github.com/esp8266/arduino-esp8266fs-plugin/releases/download/0.2.0/ESP8266FS-0.2.0.zip.
- In your Arduino sketchbook directory, create tools directory if it doesn't exist yet
- Unpack the tool into tools directory (the path will look like <home_dir>/Arduino/tools/ESP8266FS/tool/esp8266fs.jar)
- Restart Arduino

#### 3. Compile and Upload

- Open EmonESP.ino in the Arduino IDE.
- Compile and Upload as normal
- Upload home.html web page using the ESP8266 Sketch Data Upload tool under Arduino tools.


***



### Licence

GNU General Public License
