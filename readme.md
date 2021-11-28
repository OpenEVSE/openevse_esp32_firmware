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

#### Manual Override API

Manual override can be used to override a charging timer or to immediately start a charge if the EVSE is in sleeping state.

Enable Manual Override:

`curl 'http://openevse-xxx/override' --data-raw '{"state":"disabled"}' `

Disable Manual Override: 

`curl 'http://openevse-xxx/override' -X 'DELETE'`

View Manual Override status:

http://openevse-xxx/override

#### Schedule timers API

Example 

```bash
curl 'http://openevse-xxx/schedule' \
  --data-raw '[{"id":1,"state":"active","days":["monday","tuesday","wednesday","thursday","friday","saturday","sunday"],"time":"07:00"},{"id":2,"state":"disable","days":["monday","tuesday","wednesday","thursday","friday","saturday","sunday"],"time":"10:00"}]' 
```

View schedule timers:

http://openevse-xxx/schedule

Remove shedule timers:

` curl 'http://192.168.0.104/schedule/1' -X 'DELETE'`
` curl 'http://192.168.0.104/schedule/2' -X 'DELETE'`


#### Status API



Current status of the OpenEVSE in JSON format is available via: `http://openevse-xxxx/status` e.g

```json
{"mode":"STA","wifi_client_connected":1,"eth_connected":0,"net_connected":1,"srssi":-73,"ipaddress":"192.168.1.43","emoncms_connected":1,"packets_sent":22307,"packets_success":22290,"mqtt_connected":1,"ohm_hour":"NotConnected","free_heap":203268,"comm_sent":335139,"comm_success":335139,"rapi_connected":1,"amp":0,"pilot":32,"temp1":282,"temp2":-2560,"temp3":-2560,"state":254,"elapsed":3473,"wattsec":22493407,"watthour":51536,"gfcicount":0,"nogndcount":0,"stuckcount":0,"divertmode":1,"solar":390,"grid_ie":0,"charge_rate":7,"divert_update":0,"ota_update":0,"time":"2020-05-12T17:53:48Z","offset":"+0000"}
``` 

#### Config API

Current config of the OpenEVSE in JSON format is available via `http://openevse-xxxx/config` e.g

```json
{"firmware":"6.2.1.EU","protocol":"5.1.0","espflash":4194304,"version":"3.1.0.dev","diodet":0,"gfcit":0,"groundt":0,"relayt":0,"ventt":0,"tempt":0,"service":2,"scale":220,"offset":0,"ssid":"<SSID>","pass":"_DUMMY_PASSWORD","emoncms_enabled":true,"emoncms_server":"https://emoncms.org","emoncms_node":"emonevse","emoncms_apikey":"_DUMMY_PASSWORD","emoncms_fingerprint":"","mqtt_enabled":true,"mqtt_protocol":"mqtt","mqtt_server":"emonpi","mqtt_port":1883,"mqtt_reject_unauthorized":true,"mqtt_topic":"emon/openevse-55ad","mqtt_user":"emonpi","mqtt_pass":"_DUMMY_PASSWORD","mqtt_solar":"emon/solarpv/test","mqtt_grid_ie":"","mqtt_supported_protocols":["mqtt","mqtts"],"http_supported_protocols":["http","https"],"www_username":"open","www_password":"_DUMMY_PASSWORD","hostname":"openevse-55ad","time_zone":"Europe/Lisbon|WET0WEST,M3.5.0/1,M10.5.0","sntp_enabled":true,"sntp_host":"pool.ntp.org","ohm_enabled":false}
```

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
