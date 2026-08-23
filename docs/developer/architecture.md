# Firmware architecture

The OpenEVSE ESP32 WiFi gateway firmware runs on an ESP32 module connected via
the RAPI serial protocol to an OpenEVSE ATmega/SAMD controller. It provides the
web UI, HTTP + WebSocket API, MQTT, EmonCMS logging, solar divert, OCPP 1.6,
scheduling, session limits, and local energy data logging.

```
┌─────────────┐  RAPI (serial)  ┌──────────────────────────────────────┐
│  OpenEVSE    │◄───────────────►│  ESP32 WiFi gateway (this firmware)  │
│  controller  │                 │  web UI · HTTP/WS · MQTT · OCPP ·    │
│  (ATmega/    │                 │  divert · scheduler · energy logs    │
│   SAMD)      │                 └──────────────────────────────────────┘
└─────────────┘                        ▲ WiFi / wired Ethernet
                                       ▼
                              browser · MQTT broker · EmonCMS · OCPP CSMS
```

## Initialisation sequence

Each subsystem has a `.begin()` call in `setup()` (`src/main.cpp`) with
explicit dependencies. Order matters:

1. `ESPAL.begin()` — hardware abstraction (pin mapping, board ID)
2. `LittleFS.begin()` — filesystem (auto-formats on failure)
3. `config_load_settings()` — reads EEPROM (4096 bytes, config at offset 0, factory at 3072)
4. `evse.begin()` — EvseManager + RAPI serial to the controller
5. `scheduler`, `divert`, `limit`, `lcd`, `rfid`, `ledManager`
6. `net.begin()` → `Mongoose.begin()` → `web_server_setup()`
7. `energyLogger.begin()`, `mqtt.begin()`, `ocpp.begin()`, `shaper.begin()`, `tempThrottle.begin()`

`loop()` drives: `Mongoose.poll(0)` (non-blocking network I/O),
`MicroTask.update()` (cooperative task scheduler), the RAPI sender, EmonCMS
publishing, and a 30-second Ohm Connect check.

## EvseManager and the client/priority system

`EvseManager` (`src/evse_man.h`) is the central arbiter for charging state.
Every subsystem that wants to control the EVSE registers as a **client** with a
numeric priority (higher number = higher authority):

| Priority | Client |
|---|---|
| 10 | Default |
| 50 | Divert |
| 100 | Timer / Scheduler |
| 200 | Boost |
| 500 | API / MQTT / Ohm |
| 1000 | Manual |
| 1030 | RFID |
| 1050 | OCPP |
| 1100 | Limit |
| 5000 | Safety |
| 10000 | Error |

Client IDs are constructed with the `EVC(Vendor, Client)` macro. Each client
submits an `EvseProperties` (desired state, charge current, max current,
`auto_release` flag); the highest-priority claim that is active wins. Claims
are visible at runtime via the `/claims` HTTP endpoint.

## RAPI integration pattern

All communication with the controller goes through async RAPI calls with
lambda callbacks:

```cpp
_openevse.setCurrentCapacity(amps, false, [this, callback](int ret, long pilot) {
  if (RAPI_RESPONSE_OK == ret) {
    _pilot = pilot;
    _settings_changed.Trigger();
  }
  if (callback) { callback(ret); }
});
```

Always check `RAPI_RESPONSE_OK` before acting on results, and always invoke
callbacks — including on error paths. Full RAPI command reference:
[rapi.md](../rapi.md).

## Configuration management

Every config option appears in exactly three places:

1. **Extern declaration** in `src/app_config.h`
2. **Variable definition** in `src/app_config.cpp` (top of file)
3. **`ConfigOptDefinition` entry** in the `opts[]` array in `src/app_config.cpp`

Boolean flags use bit positions in a `uint32_t flags` variable with `#define`
macros. Change notifications use `onChanged()` callbacks with prefix matching
(e.g. `name.startsWith("mqtt_")`) to fan out updates to affected subsystems.

The divert simulator's `test_config.py` suite asserts default config values —
update those assertions whenever changing defaults in `app_config.cpp`.

## Key subsystems

| File(s) | Role |
|---|---|
| `evse_man.h/.cpp` | Central EVSE arbitration, client/priority system |
| `evse_monitor.cpp` | RAPI polling, translates hardware state to EvseManager |
| `app_config.h/.cpp` | All runtime configuration, EEPROM persistence |
| `web_server.cpp` + `web_server_*.cpp` | Mongoose HTTP server, split into handler modules |
| `mqtt.h/.cpp` | MQTT pub/sub (MongooseMqttClient), 50 ms poll loop |
| `emoncms.h/.cpp` | Posts to an EmonCMS server (default `https://emoncms.org`) |
| `divert.h/.cpp` | Solar/eco divert algorithm; `solar` and `grid_ie` globals (W) |
| `energy_meter.h/.cpp` | Session/daily/weekly/monthly/yearly kWh counters → `/emeter.json` |
| `energy_logger.h/.cpp` | 60-second time-series samples → `/logs/` on LittleFS |
| `scheduler.h/.cpp` | Weekly charge schedule with start/stop windows |
| `limit.h/.cpp` | Session energy/time/SOC/range limits |
| `current_shaper.h/.cpp` | Grid-level power cap enforcement with smoothing |
| `temp_throttle.h/.cpp` | Current reduction on over-temperature |
| `ocpp.h/.cpp` | OCPP 1.6 via the MicroOcpp library |
| `rfid.h/.cpp` | RFID card auth, PN532 NFC module (optional) |
| `net_manager.h/.cpp` | WiFi / wired Ethernet, OTA capability |
| `lcd.h/.cpp`, `lcd_tft.h/.cpp` | Character LCD and TFT touchscreen display |
| `time_man.h/.cpp` | SNTP sync, POSIX timezone strings |
| `certificates.h/.cpp` | SSL cert store under `/certificates/` on LittleFS |
| `tesla_client.h/.cpp` | Tesla API (SOC, range, ETA) |
| `ohm.h/.cpp` | Ohm Connect demand-response integration |

## Energy logging

`EnergyLogger` (`src/energy_logger.h`) runs as a MicroTasks task, sampling
every 60 seconds. Storage under `/logs/` on LittleFS (hard cap 20 KB total):

- `/logs/raw/` — rolling 6-hour raw buffer (2 × 3-hour chunk files, ~18 KB)
- `/logs/daily/YYYY-QN.json` — daily metrics aggregated quarterly, 6-month retention
- `/logs/monthly/YYYY.json` — monthly metrics, ≤12 entries per year
- `/logs/annual.json` — annual metrics

`EnergyMeter` persists running counters (session/daily/weekly/monthly/yearly
kWh) to `/emeter.json`, saving every 5 minutes while charging and rotating on
date boundaries.

## Divert simulator

`divert_sim/` is a standalone C++ + Python test suite that validates the solar
divert and current shaper algorithms without hardware. Run with `pytest -v`
from `divert_sim/` (see [building.md](building.md) for the host build).

## Coding conventions

- **Config variables**: `snake_case` (e.g. `mqtt_server`, `emoncms_server`)
- **Private members**: `_snake_case` with leading underscore
- **Constants / `#define`**: `UPPER_SNAKE_CASE`
- **Classes**: `PascalCase`
- **Timeout comparisons**: use a signed cast to survive the 49-day `millis()`
  rollover: `(long)(millis() - timeout) >= 0`
- **Debug output**: `DBUGLN()`, `DBUGF()`, `DBUGVAR()` macros; wrap in `#ifdef ENABLE_DEBUG`
- **Structured events**: `event_send()` with a JSON document — publishes to web
  clients and MQTT simultaneously
- **Task return value**: return milliseconds until next wake from `MicroTasks::Task::loop()`
