# OpenEVSE Developers Guide — MQTT API

This guide describes the MQTT API implemented by the OpenEVSE WiFi gateway firmware (ESP32, v4.x and later). It covers device discovery, the topics the charging station publishes, the command topics it subscribes to, RAPI passthrough over MQTT, and the integration topics used for solar PV divert, current shaping and vehicle data.

The companion HTTP API is documented in the [OpenEVSE WiFi API reference](https://openevse.stoplight.io/docs/openevse-wifi-v4) ([api.yml](../api.yml)). The MQTT command topics accept the same JSON payloads as the equivalent HTTP endpoints, so the two APIs can be used interchangeably.

- [Prerequisites](#prerequisites)
- [MQTT setup](#mqtt-setup)
- [Connection behaviour](#connection-behaviour)
- [Device discovery and Last Will (announce topic)](#device-discovery-and-last-will-announce-topic)
- [Published topics — status values](#published-topics--status-values)
- [Published topics — retained JSON state](#published-topics--retained-json-state)
- [Command topics (subscribed by OpenEVSE)](#command-topics-subscribed-by-openevse)
- [RAPI over MQTT](#rapi-over-mqtt)
- [Integration input topics (topics you publish, OpenEVSE consumes)](#integration-input-topics-topics-you-publish-openevse-consumes)
- [MQTT configuration reference](#mqtt-configuration-reference)
- [Worked examples](#worked-examples)
- [Connecting to cloud IoT services](#connecting-to-cloud-iot-services)
- [Home Assistant](#home-assistant)
- [Additional resources](#additional-resources)
- [License](#license)

## Prerequisites

- OpenEVSE / EmonEVSE charging station with the ESP32 WiFi gateway (firmware v4.x or later)
- Gateway connected to a network (WiFi station mode or wired Ethernet)
- An MQTT broker reachable from the gateway, e.g. [Mosquitto](https://mosquitto.org/), [emonPi](https://guide.openenergymonitor.org/technical/mqtt/), or a cloud broker such as AWS IoT Core
- Plain MQTT (default port 1883) and MQTT over TLS (MQTTS, default port 8883) are both supported

The examples in this guide use the [mosquitto command line clients](https://mosquitto.org/download/) (`mosquitto_pub` / `mosquitto_sub`).

## MQTT setup

Using the web interface:

1. Browse to the IP address or hostname of the OpenEVSE gateway (e.g. `http://openevse-xxxx.local`)
2. Open the **Services** tab
3. Tick **Enable MQTT**
4. Enter the broker hostname or IP address, and port
5. (Optional) Enter the broker username and password
6. (Optional) Change the **base-topic** OpenEVSE publishes to (default: the device hostname, e.g. `openevse-7b2c`)
7. Save; the status should change to *Connected: Yes* within a few seconds

MQTT can also be configured programmatically via the HTTP API (`POST /config`) or, once connected, via the `<base-topic>/config/set` MQTT topic. The relevant configuration keys are listed in [MQTT configuration reference](#mqtt-configuration-reference).

> **Note:** The MQTT settings are pre-populated with the OpenEnergyMonitor [emonPi default MQTT credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt). If you are posting to an emonPi broker and want the data to appear in Emoncms, use `emon/<name>` as the base-topic.

## Connection behaviour

| Property | Behaviour |
| --- | --- |
| Client ID | The device hostname (`openevse-xxxx` by default) |
| Protocol | `mqtt` (plain TCP) or `mqtts` (TLS), selected by the `mqtt_protocol` config key |
| Authentication | Optional username/password (`mqtt_user` / `mqtt_pass`) |
| TLS server verification | Enabled by default; can be disabled with `mqtt_reject_unauthorized: false` (not recommended) |
| TLS client certificate | Optional; upload a certificate/key via the HTTP certificates API and reference it with `mqtt_certificate_id` |
| Reconnect | Automatic, attempted every 5 seconds while the network is up and MQTT is enabled |
| Last Will & Testament | Retained *disconnected* message on the announce topic (see below) |

On (re)connection the firmware publishes a retained announce message, subscribes to all of its command topics, and re-publishes its full retained state (config, claim, override, schedule, limit).

## Device discovery and Last Will (announce topic)

At startup the following message is published **retained** to the announce topic, `openevse/announce/xxxx` by default, where `xxxx` is the last 4 characters of the device ID (configurable via `mqtt_announce_topic`):

```json
{
  "state": "connected",
  "id": "c44f330dxxad",
  "name": "openevse-7b2c",
  "mqtt": "openevse-7b2c",
  "http": "http://192.168.1.43/"
}
```

- `id` — the unique device ID
- `name` — the device hostname
- `mqtt` — the base-topic this device publishes to (use this to find its status topics)
- `http` — the URL of the device's HTTP API / web interface

To discover all OpenEVSE devices on a broker, subscribe with a wildcard:

```bash
mosquitto_sub -h broker.local -t 'openevse/announce/#' -v
```

The same topic is registered as the connection's Last Will & Testament. If the device disconnects ungracefully the broker publishes (retained):

```json
{"state": "disconnected", "id": "c44f330dxxad", "name": "openevse-7b2c"}
```

Because both messages are retained, the announce topic always reflects the device's last known connection state and can be used as an availability topic.

## Published topics — status values

Status values are published as **individual sub-topics of the base-topic**, one value per topic:

```text
<base-topic>/<name> <value>
```

Publishing is **event driven**: values are published when the gateway polls the EVSE controller and the data changes (typically every few seconds while charging), not on a fixed 30 second timer. Messages are published non-retained by default; set the `mqtt_retained` config key to `true` to publish these status values with the retained flag.

### Electrical and sensor values

| Topic | Description |
| --- | --- |
| `<base>/amp` | Measured charge current in **milliamps** |
| `<base>/voltage` | Voltage in volts (measured, or configured/MQTT-supplied value) |
| `<base>/power` | Charge power in watts |
| `<base>/frequency` | AC line frequency in 100ths of Hz, e.g. `5000` = 50.00 Hz (only on controllers that measure it) |
| `<base>/pilot` | Pilot current advertised to the vehicle, in amps |
| `<base>/max_current` | Effective maximum charge current in amps (after claims/limits) |
| `<base>/temp` | Primary temperature reading in **10ths of °C** (e.g. `247` = 24.7 °C), `false` if not valid |
| `<base>/temp_max` | Highest of the available temperature sensors, 10ths of °C |
| `<base>/temp1` … `<base>/temp3` | Individual temperature sensors (DS3232, MCP9808, TMP007), 10ths of °C, `false` if not fitted |
| `<base>/srssi` | WiFi signal strength (RSSI, dBm) |
| `<base>/freeram` | Free heap memory of the gateway, bytes |

### Charge state

| Topic | Description |
| --- | --- |
| `<base>/state` | Numeric EVSE state (see table below) |
| `<base>/status` | EVSE service state as a string: `active` or `disabled` |
| `<base>/flags` | EVSE controller flags bitmask |
| `<base>/vehicle` | `1` if a vehicle is connected, `0` otherwise |
| `<base>/colour` | Current LCD/LED colour code |
| `<base>/manual_override` | `1` if a manual override is active, `0` otherwise |
| `<base>/evse_connected` | `1` if the gateway has serial communication with the EVSE controller |
| `<base>/divertmode` | Divert mode: `1` = normal, `2` = eco (solar divert) |
| `<base>/rfid_auth` | Last authenticated RFID tag (only when RFID is enabled) |

EVSE state values (`<base>/state`) follow the OpenEVSE controller states:

| Value | State |
| --- | --- |
| 0 | Starting |
| 1 | Not connected (ready) |
| 2 | Vehicle connected |
| 3 | Charging |
| 4–11 | Error states (vent required, diode check failed, GFCI fault, no earth ground, stuck relay, GFCI self-test failed, over temperature, over current) |
| 254 | Sleeping |
| 255 | Disabled |

### Session and energy values

| Topic | Description |
| --- | --- |
| `<base>/session_elapsed` | Elapsed time of the current charge session, seconds |
| `<base>/session_energy` | Energy delivered in the current session, **Wh** |
| `<base>/total_energy` | Lifetime total energy, **kWh** |
| `<base>/total_day` | Energy today, kWh |
| `<base>/total_week` | Energy this week, kWh |
| `<base>/total_month` | Energy this month, kWh |
| `<base>/total_year` | Energy this year, kWh |
| `<base>/total_switches` | Total number of relay switch cycles |
| `<base>/time` / `<base>/local_time` | Current time (UTC / local, ISO 8601) |
| `<base>/offset` | Timezone offset |
| `<base>/uptime` | Gateway uptime, seconds |

Deprecated (will be removed in a future release — use the `session_*`/`total_*` topics instead): `<base>/elapsed` (seconds), `<base>/wattsec` (session energy in watt-seconds), `<base>/watthour` (total energy in Wh).

### Vehicle and home battery values

When vehicle or home-battery data is fed into the gateway (via MQTT input topics, HTTP or Tesla API), it is re-published on:

| Topic | Description |
| --- | --- |
| `<base>/battery_level` | Vehicle state of charge, % |
| `<base>/battery_range` | Vehicle range (km or miles, per `mqtt_vehicle_range_miles`) |
| `<base>/time_to_full_charge` | Vehicle time to full charge, seconds |
| `<base>/vehicle_charge_limit` | Vehicle charge limit, % |
| `<base>/home_battery_soc` | Home/storage battery state of charge, % |
| `<base>/home_battery_power` | Home/storage battery power, W |

### Solar divert and current shaper values

When divert mode is enabled, divert status is published on `<base>/grid_ie` or `<base>/solar` (echo of the last received input value), plus `<base>/charge_rate`, `<base>/available_current`, `<base>/smoothed_available_current`, `<base>/divert_active`, `<base>/trigger_current` and `<base>/min_charge_end` as the divert algorithm updates. When the current shaper is enabled its status is published on `<base>/shaper` (`0`/`1`), `<base>/shaper_live_pwr`, `<base>/shaper_smoothed_live_pwr`, `<base>/shaper_max_pwr` and `<base>/shaper_cur`.

## Published topics — retained JSON state

In addition to the individual status values, the firmware maintains a set of **retained** JSON documents (always retained, regardless of `mqtt_retained`). Each is re-published whenever the underlying state changes — including changes made via the HTTP API or web UI — so subscribing to these topics gives you the current state immediately plus live updates:

| Topic | Content |
| --- | --- |
| `<base>/config` | Full device configuration as a JSON object (secrets such as passwords are redacted) |
| `<base>/config_version` | Configuration version counter |
| `<base>/claim` | The current MQTT-service claim, or `{"state":"null"}` when none |
| `<base>/override` | The current manual override, or `{"state":"null"}` when none |
| `<base>/schedule` | The charge scheduler configuration |
| `<base>/limit` | The active charge limit, or an empty/none limit when not set |

## Command topics (subscribed by OpenEVSE)

All commands are sub-topics of the base-topic. JSON payloads use exactly the same schemas as the HTTP API — the linked reference documentation applies to both.

| Topic | Payload | Action |
| --- | --- | --- |
| `<base>/override/set` | JSON [override properties](https://openevse.stoplight.io/docs/openevse-wifi-v4/e0ab0a4ad5e1e-set-the-manual-override) | Set/update the manual override |
| `<base>/override/set` | `toggle` | Toggle the manual override (acts like pressing the button) |
| `<base>/override/set` | `clear` | Clear the manual override |
| `<base>/claim/set` | JSON [claim properties](https://openevse.stoplight.io/docs/openevse-wifi-v4/ebc578ffa7ca7-make-update-an-evse-claim) | Make/update the MQTT service claim (same priority as the HTTP client claim) |
| `<base>/claim/set` | `release` | Release the MQTT service claim |
| `<base>/schedule/set` | JSON [schedule events](https://openevse.stoplight.io/docs/openevse-wifi-v4/e87e6f3f90787-batch-update-schedule) | Set/update scheduled charge events |
| `<base>/schedule/clear` | event id (integer) | Remove one schedule event |
| `<base>/limit/set` | JSON [limit properties](https://openevse.stoplight.io/docs/openevse-wifi-v4/c410fb5e48294-set-charge-limit) | Set a session limit (`type`: `time`, `energy`, `soc` or `range`) |
| `<base>/limit/set` | `clear` | Clear the session limit |
| `<base>/config/set` | JSON config object (any subset of [config keys](../models/Config.yaml)) | Update device configuration |
| `<base>/divertmode/set` | `1` or `2` | Set divert mode: `1` = normal, `2` = eco |
| `<base>/shaper/set` | `0` or `1` | Temporarily disable (`0`) / enable (`1`) the current shaper (does not survive a reboot) |
| `<base>/restart` | `{"device":"gateway"}` or `{"device":"evse"}` | Restart the WiFi gateway or the EVSE controller |
| `<base>/rapi/in/$CC …` | see [RAPI over MQTT](#rapi-over-mqtt) | Send a raw RAPI command to the EVSE controller |

Notes:

- After each command the corresponding retained state topic (`<base>/override`, `<base>/claim`, `<base>/schedule`, `<base>/limit`, `<base>/config`) is re-published, which serves as the acknowledgement.
- Claim and override properties can be set incrementally: a JSON payload containing only some fields updates just those fields. To remove a single property, send `"clear"` as its value, e.g. `{"charge_current": "clear"}`.
- Property values follow the [Properties schema](../models/Properties.yaml): `state` (`active`/`disabled`), `charge_current` (A), `max_current` (A), `auto_release` (bool).

## RAPI over MQTT

RAPI is the low-level serial command protocol of the OpenEVSE controller. The gateway forwards RAPI commands received over MQTT to the controller and publishes the response.

> **Important:** Prefer the higher-level topics above (`claim`, `override`, `limit`, …) where they exist — they cooperate with the gateway's claims system, whereas raw RAPI commands bypass it. See [docs/rapi.md](../docs/rapi.md) for details and the full command reference; the complete command list lives in [`rapi_proc.h` in the OpenEVSE controller firmware](https://github.com/OpenEVSE/open_evse/blob/stable/firmware/open_evse/src/rapi_proc.h). Some potentially unsafe commands are blocked by the gateway.

Request:

```text
<base-topic>/rapi/in/$<command> [payload = parameters]
```

The command is taken from the topic (everything from the `$`); if the message payload is non-empty it is appended as the command's parameters.

Response — the raw RAPI response string (e.g. `$OK 32 0001^22`) is published to:

```text
<base-topic>/rapi/out
```

Examples (base-topic `openevse-7b2c`):

```bash
# Watch for responses
mosquitto_sub -h broker.local -t 'openevse-7b2c/rapi/out'

# Get the current charge current setting ($GC = get current capacity)
mosquitto_pub -h broker.local -t 'openevse-7b2c/rapi/in/$GC' -n

# Set charge current to 13 A ($SC), parameters in the payload
mosquitto_pub -h broker.local -t 'openevse-7b2c/rapi/in/$SC' -m '13'
```

Responses are published to the single shared `rapi/out` topic in the order the commands complete; there is no per-request correlation ID, so serialise your requests if you need to match responses to commands.

## Integration input topics (topics you publish, OpenEVSE consumes)

These are **your** topics — full topic paths configured in the OpenEVSE settings (they are *not* under the base-topic). The gateway subscribes to them and consumes the values. All payloads are plain numbers.

| Config key | Payload | Used for |
| --- | --- | --- |
| `mqtt_solar` | Solar PV generation, **watts** (positive) | Solar divert (when `divert_type` = solar) |
| `mqtt_grid_ie` | Grid import/export, **watts**; positive = importing, **negative = exporting** | Solar divert (when `divert_type` = grid import/export) |
| `mqtt_live_pwr` | Site live power, watts | Current shaper (may be the same topic as `mqtt_grid_ie`) |
| `mqtt_vrms` | AC voltage, volts | Voltage used for power/energy calculation |
| `mqtt_vehicle_soc` | Vehicle state of charge, % | Vehicle status / SoC limits |
| `mqtt_vehicle_range` | Vehicle range (km, or miles if `mqtt_vehicle_range_miles`) | Vehicle status / range limits |
| `mqtt_vehicle_eta` | Time to full charge, seconds | Vehicle status |
| `mqtt_vehicle_charge_limit` | Vehicle charge limit, % | Vehicle status |
| `mqtt_home_battery_soc` | Home battery state of charge, % | Display only |
| `mqtt_home_battery_power` | Home battery power, W | Display only |

Notes:

- Divert topics are only subscribed when divert is enabled (`divert_enabled: true`) and the matching `divert_type` is selected; the shaper topic requires `current_shaper_enabled: true`.
- Vehicle topics are only acted on when `vehicle_data_src` is set to `2` (MQTT). (`0` = none, `1` = Tesla API, `2` = MQTT, `3` = HTTP.)
- **Solar divert example:** a house exporting 1077 W to the grid should publish `-1077` to the grid I/E topic (or `1077` plus house load to the solar topic). OpenEVSE then adjusts the charge rate to consume the surplus. See the [divert simulator](../divert_sim) for the algorithm details.

## MQTT configuration reference

All keys below can be set via the web UI, `POST /config` (HTTP) or `<base>/config/set` (MQTT), and are visible in the retained `<base>/config` topic. See [models/Config.yaml](../models/Config.yaml) for the full config schema.

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `mqtt_enabled` | bool | `false` | Enable the MQTT service |
| `mqtt_protocol` | string | `mqtt` | `mqtt` or `mqtts` (TLS). Read-only companion `mqtt_supported_protocols` lists `["mqtt","mqtts"]` |
| `mqtt_server` | string | `emonpi` | Broker hostname or IP |
| `mqtt_port` | int | `1883` | Broker port (typically `8883` for MQTTS) |
| `mqtt_user` | string | `emonpi` | Username (empty for anonymous) |
| `mqtt_pass` | string | `emonpimqtt2016` | Password (write-only; redacted in config output) |
| `mqtt_topic` | string | hostname | Base-topic for all publish/subscribe topics |
| `mqtt_announce_topic` | string | `openevse/announce/<id>` | Discovery/LWT topic |
| `mqtt_retained` | bool | `false` | Publish status values with the retained flag |
| `mqtt_reject_unauthorized` | bool | `true` | Verify the broker's TLS certificate (MQTTS) |
| `mqtt_certificate_id` | string | `""` | ID of a client certificate/key pair for mutual TLS (see the HTTP certificates API) |
| `mqtt_solar` | string | `""` | Solar generation input topic |
| `mqtt_grid_ie` | string | `emon/emonpi/power1` | Grid import/export input topic |
| `mqtt_vrms` | string | `emon/emonpi/vrms` | Voltage input topic |
| `mqtt_live_pwr` | string | `""` | Live power input topic for the current shaper |
| `mqtt_vehicle_soc` | string | `""` | Vehicle SoC input topic |
| `mqtt_vehicle_range` | string | `""` | Vehicle range input topic |
| `mqtt_vehicle_eta` | string | `""` | Vehicle time-to-full-charge input topic |
| `mqtt_vehicle_charge_limit` | string | `""` | Vehicle charge limit input topic |
| `mqtt_vehicle_range_miles` | bool | `false` | Interpret/display vehicle range in miles |
| `mqtt_home_battery_soc` | string | `""` | Home battery SoC input topic |
| `mqtt_home_battery_power` | string | `""` | Home battery power input topic |
| `vehicle_data_src` | int | `0` | Vehicle data source: `0` none, `1` Tesla, `2` MQTT, `3` HTTP |

## Worked examples

All examples assume broker `broker.local` and base-topic `openevse-7b2c`. Add `-u <user> -P <pass>` as needed.

Watch everything the device publishes:

```bash
mosquitto_sub -h broker.local -t 'openevse-7b2c/#' -v
```

Start charging now (manual override):

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/override/set' -m '{"state":"active"}'
```

Stop/pause charging:

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/override/set' -m '{"state":"disabled"}'
```

Charge at 16 A while the override is active:

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/override/set' -m '{"state":"active","charge_current":16}'
```

Clear the override (return to normal/scheduled behaviour):

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/override/set' -m 'clear'
```

Limit this session to 10 kWh, then auto-release:

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/limit/set' \
  -m '{"type":"energy","value":10000,"auto_release":true}'
```

Cap the current via a service claim (cooperates with other services rather than overriding them):

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/claim/set' -m '{"max_current":20}'
# ...later
mosquitto_pub -h broker.local -t 'openevse-7b2c/claim/set' -m 'release'
```

Schedule charging between 07:00 and 10:00 every day:

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/schedule/set' -m '[
  {"id":1,"state":"active","time":"07:00:00","days":["monday","tuesday","wednesday","thursday","friday","saturday","sunday"]},
  {"id":2,"state":"disabled","time":"10:00:00","days":["monday","tuesday","wednesday","thursday","friday","saturday","sunday"]}]'
```

Feed grid import/export data for solar divert (from your energy monitor, every few seconds):

```bash
mosquitto_pub -h broker.local -t 'emon/emonpi/power1' -m '-1077'   # exporting 1077 W
```

Switch divert to eco mode:

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/divertmode/set' -m '2'
```

Restart the gateway:

```bash
mosquitto_pub -h broker.local -t 'openevse-7b2c/restart' -m '{"device":"gateway"}'
```

## Connecting to cloud IoT services

For TLS brokers select the `mqtts` protocol and port 8883 (or as required by the service). If the service uses client-certificate authentication, upload the certificate and private key via the HTTP certificates API and set `mqtt_certificate_id`.

### AWS IoT Core

Example policy for AWS IoT Core:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "arn:aws:iot:eu-west-2:489072314047:client/openevse-*"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Subscribe",
      "Resource": "arn:aws:iot:eu-west-2:489072314047:topicfilter/openevse/*"
    },
    {
      "Effect": "Allow",
      "Action": [
        "iot:Receive",
        "iot:Publish",
        "iot:RetainPublish"
      ],
      "Resource": "arn:aws:iot:eu-west-2:489072314047:topic/openevse/*"
    }
  ]
}
```

## Home Assistant

The firmware does **not** publish Home Assistant MQTT auto-discovery messages. For Home Assistant, use the native [OpenEVSE integration](https://github.com/firstof9/openevse) (HTTP based), or define [MQTT sensors/switches manually](https://www.home-assistant.io/integrations/sensor.mqtt/) against the topics documented in this guide. The retained announce topic works well as an MQTT availability topic.

## Additional resources

- [MQTT API summary](../docs/mqtt.md)
- [RAPI reference](../docs/rapi.md)
- [HTTP API reference (OpenAPI)](https://openevse.stoplight.io/docs/openevse-wifi-v4) / [api.yml](../api.yml)
- [Config schema](../models/Config.yaml), [Properties schema](../models/Properties.yaml), [Claim schema](../models/Claim.yaml), [Limit schema](../models/Limit.yaml)
- [OpenEVSE WiFi user guide](../docs/user-guide.md)
- [OpenEVSE controller firmware (RAPI implementation)](https://github.com/OpenEVSE/open_evse)

## License

This document is part of the OpenEVSE project and is released under the Creative Commons Attribution-ShareAlike (CC BY-SA) license. The OpenEVSE firmware source code is released under the GNU General Public License v3.
