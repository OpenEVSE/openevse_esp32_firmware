# OpenEVSE WiFi ESP32 Gateway v4

> **_NOTE:_** Breaking change! This release reccomends a minimum of [7.1.3](https://github.com/OpenEVSE/open_evse/releases) of the OpenEVSE firmware, features including Solar Divert and push button menus may not behave as expected on older firmware.

- *For the older WiFi V2.x ESP8266 version (pre June 2020), see the [v2 firmware repository](https://github.com/openevse/ESP8266_WiFi_v2.x/)*


![main](docs/main2.png)

The WiFi gateway uses an **ESP32** which communicates with the OpenEVSE controller via serial RAPI API. The web UI is served directly from the ESP32 web server and can be controlled via a connected device on the local network.

**This FW also supports wired Ethernet connection using [ESP32 Gateway](docs/wired-ethernet.md)**

**[Live UI demo](https://openevse.openenergymonitor.org)**

***

## Contents

<!-- toc -->

- [Features](#features)
- [Requirements](#requirements)
- [User Guide](docs/user-guide.md)
- [Firmware Development Guide](docs/development-guide.md)
- [API](https://openevse.stoplight.io/studio/openevse-wifi-v4)
- [About](#about)
- [Licence](#licence)

<!-- tocstop -->

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
  - OpenEVSE FW [V7.1.3+ recommended](https://github.com/OpenEVSE/open_evse/releases)
  - All new OpenEVSE units are shipped with V7.1.3 pre-loaded (April 2021 onwards)


### ESP32 WiFi Module

- **Note: WiFi module is included as standard in most OpenEVSE units**
- Purchase via: [OpenEVSE Store (USA/Canda)](https://store.openevse.com/collections/frontpage/products/openevse-wifi-kit) | [OpenEnergyMonitor (UK / EU)](https://shop.openenergymonitor.com/openevse-wifi-gateway/)
- See [OpenEVSE WiFi setup guide](https://openevse.dozuki.com/Guide/WiFi+-+Join+Network/29) for basic instructions

### Web browsing device

- Mobile phone, tablet, desktop computer, etc.: any device that can display web pages and can network via WiFi.
*Note: Use of Internet Explorer 11 or earlier is not recommended*

***

### HTTP API (Recommended)

### Tesla API

**BETA**

**Polling the Tesla API will keep the car awake which will increase vampire drain, see [Issue #96](https://github.com/OpenEVSE/ESP32_WiFi_V3.x/issues/96).**

WiFi firmware V3.2 includes basic Tesla API integration. The HTTP API for this is as follows:

```http
POST {{baseUrl}}/config HTTP/1.1
Content-Type: application/json

{
  "tesla_enabled": true,
  "tesla_username": "username",
  "tesla_password": "password"
}

```

```http
POST {{baseUrl}}/config HTTP/1.1
Content-Type: application/json

{
  "tesla_vehidx": 0
}
```

Example using cURL:

```
curl --request POST \
  --url http://openevse-xxxx/config \
  --header 'content-type: application/json' \
  --header 'user-agent: vscode-restclient' \
  --data '{"tesla_enabled": true,"tesla_username": "username","tesla_password": "password"}'
```

Return a list of vehicles associated with the Tesla account e.g

`http://openevse-xxxx/teslaveh`

e.g
`{"count:"2,[{"id":"xxxx","name":"tesla1"},{"id":"xxxxx","name":"tesla2"}]}`

*Note: The vehicle ID starts at zero so the first car will have vi=0*

The SoC and rated range of the Tesla vehicle is now displayed in JSON format via `/status` and posted to MQTT. 

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
