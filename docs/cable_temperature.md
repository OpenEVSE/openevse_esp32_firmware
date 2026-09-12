# Cable NTC temperature monitoring

Surfaces the OpenEVSE controller's cable thermistor monitoring through the
gateway: controller firmware → RAPI → client library → this firmware → HTTP
API / MQTT. The controller shuts charging down on over-temperature by itself;
this layer reads, reports and configures it.

Spans three repos:

| Repo | Branch/tag | What it adds |
|---|---|---|
| `open_evse` (ATmega/SAMD controller) | `dev` @ `64ce6a4`, version 9.4.0 | `CableTempMonitor` module, `$GN`/`$SN`/`$FF C`, over-temperature fault |
| `OpenEVSE_Lib` (client library) | `OpenEVSE9` @ `df49497`, 0.0.23 | `getCableTemperatures()`, `getCableTemperatureConfig()`, `setCableTemperatureConfig()`, `setCableTemperaturePin()` |
| `openevse_esp32_firmware` (this repo) | see below | Polls and caches the readings, `/cabletemp` endpoint, `cable_temp` config flag, telemetry keys |

`openevse/OpenEVSE@0.0.23` resolves from the PlatformIO registry — the
library has since been published and a plain `pio run` fetches it with no
local staging needed.

## Build flag — not enabled by default

**Gated behind `ENABLE_CABLE_TEMP`, and only the two 16MB environments set
it:** `openevse_wifi_tft_v1` (+`_dev`, via `common.build_flags_openevse_tft`)
and `openevse_wifi_v1_16mb`.

This is a flash-space constraint, not a design preference. Every other
environment — including the default `openevse_wifi_v1` — uses the 4MB
`min_spiffs.csv` layout, and that image is already at **99.7%** of its app
partition before any of this:

| Build (`openevse_wifi_v1`, 4MB) | Flash | of 1,966,080 |
|---|---|---|
| master, library 0.0.22 | 1,959,857 | 99.68% |
| master, library 0.0.23 | 1,960,293 | 99.71% |
| \+ this integration, unguarded | 1,967,825 | **100.09% — overflows by 1,745** |
| \+ this integration, guarded off (shipped) | 1,960,293 | 99.71% |

The integration costs ~7.5KB and only ~6.2KB was available. Bumping the
library alone costs 436 bytes on every board — the new methods are otherwise
dead-stripped when nothing calls them.

To enable it on a 4MB board, something else has to come out of that image
first. Add `-D ENABLE_CABLE_TEMP` to the env once there is room.

## What the controller does

Four logical sources — `EV1`/`EV2` on the EV (output) cable, `IN1`/`IN2` on
the input (supply) cable — each with its own thermistor parameters,
calibration offset and shutdown threshold, each assignable to one of two
analog inputs (`PP_READ`, `PP2_READ`) or left unassigned. The controller
faults into `EVSE_STATE_OVER_TEMPERATURE` when an assigned source reaches its
threshold. Defaults suit the Phoenix Contact NACS cable (10k NTC, 90.0 °C
shutdown).

`PP_READ` is shared with the proximity pilot: assigning a source to it makes
the controller turn PP auto-ampacity off, and enabling PP auto-ampacity makes
it unassign any source using that pin. Both happen silently, which is why
every write here re-reads the controller's settings flags afterward.

See `open_evse/docs/cable-temperature-monitoring.md` for the measurement
model, accuracy and the controller-side design decisions.

## Reading and caching (`EvseMonitor`)

- **Live readings** (`$GN`) are polled on the same cadence as the enclosure
  sensors (`getTemperatureFromEvse()`), not the slower ~60s settings poll —
  they are a safety signal and shouldn't lag `$GP`.
- **Per-source configuration** (`$GN idx`) is read at boot — all four sources,
  since nothing is known yet — and after a successful write, where only the
  written source is re-read (one call, not four: a single-source write can't
  change another source's own configuration). Never polled; it only changes
  when something writes it.
- Both are invalidated on controller boot, alongside the relay-health state
  and the four calibration arrays themselves, so a swapped or downgraded
  controller can't keep serving the old one's values.
- Boot queues `heartbeatEnable` ahead of every read-only command here
  (including cable-temp's, the largest single block of them) — losing
  heartbeat supervision to a full RAPI queue would be worse than losing a
  boot-time diagnostic reading, so if anything has to be the one that
  doesn't fit, it isn't heartbeat.
- Neither is gated on `isD9Supported()`: the library already returns
  `RAPI_RESPONSE_FEATURE_NOT_SUPPORTED` for controllers without the feature,
  so the callbacks simply leave `isCableTempKnown()` false.

A reading is only marked valid for `OPENEVSE_CABLE_TEMP_STATUS_OK`. The
library reports 0 for the three non-reading conditions, and marking those
valid would publish a bogus 0 °C to MQTT.

## HTTP API

### `GET /cabletemp`

```json
{
  "supported": true,
  "enabled": true,
  "sources": [
    { "source": 0, "name": "ev1", "pin": 2, "status": 0, "temperature": 45.2,
      "r25": 10000, "beta": 3443, "offset_c10": 0, "panic_c10": 900 },
    { "source": 1, "name": "ev2", "pin": 0, "status": 1, "r25": 10000, "beta": 3443, "offset_c10": 0, "panic_c10": 900 },
    ...
  ]
}
```

`status`: `0` ok · `1` not installed/unassigned · `2` open circuit · `3`
shorted. `temperature` is **omitted** rather than sent as 0 when there is no
reading, so a client can't mistake "no sensor" for "0 °C" — `status` says
which it is. The `r25`/`beta`/`offset_c10`/`panic_c10` fields are omitted
until the configuration has actually been read back from the controller.

An unplugged cable reports `status: 2` (open) during entirely normal
operation — open is not an error condition.

### `POST /cabletemp`

```jsonc
// reassign the input pin only, keeping the calibration
{ "source": 0, "pin": 2 }

// full configuration
{ "source": 0, "pin": 2, "r25": 10000, "beta": 3443, "offset_c10": 0, "panic_c10": 900 }
```

`source` (0-3) and `pin` (0=unassigned, 1=PP_READ, 2=PP2_READ) are required.
The four calibration fields are all-or-nothing: the controller's
full-configuration form is atomic, and filling the gaps from the local cache
would silently write back a stale value if the cache were cold or another
client had changed it. Numeric ranges are enforced by the controller, which
NAKs anything outside them.

### Why a separate endpoint

The per-source configuration is 20 fields. `/config`'s document is
`JSON_OBJECT_SIZE(128) + 1024` and its own comment records that a live TFT
unit already serves ~135 members within that budget with little room left, so
this does not belong there.

## `/config`

Only the on/off flag, on D9+ controllers that report the feature:

```json
{ "cable_temp": true }
```

The controller still has no read-back for `$FF C`, so `isCableTempEnabled()`
still has to infer it from the sources when nothing has been commanded this
session — the feature reports `NOT_INSTALLED` on every source while it is
off, which used to read as `false` right after enabling it with no source
assigned yet. `EvseMonitor` now caches the commanded value
(`_cable_temp_commanded`) and trusts it over that inference, so the flag no
longer flips back on the very GET that follows a successful enable/disable —
and `POST /config`'s equality guard, previously withheld here specifically
because of that false negative, applies to `cable_temp` like it does to its
neighbours. The cache is session-local (reset on controller boot, alongside
everything else in this feature), so a controller that already had it
enabled from a prior session with zero sources assigned still reads `false`
until a source is assigned or it's toggled again — the controller-side fix
(reporting the bit in `$GN`) is the only way to close that remaining gap,
and is still worth doing in a future controller revision.

## Telemetry (`/status`, WebSocket, MQTT, EmonCMS)

`create_rapi_json()` emits `cable_temp_ev1`, `cable_temp_ev2`,
`cable_temp_in1`, `cable_temp_in2`, scaled by `TEMP_SCALE_FACTOR` like the
existing `temp1`..`temp4`, and `false` for an assigned source that isn't
currently reading.

**Only assigned sources are emitted.** On an installation with no cable
thermistors — which is almost all of them — this adds nothing at all to the
payload, and this document is also the MQTT/EmonCMS telemetry, so it goes out
on every data-ready event. `/cabletemp` is where you find out *why* a source
isn't reading.

## Not done

- **No dedicated MQTT config topic** — configuration is HTTP-only.
- **No enable read-back**, see the `/config` limitation above.

## GUI (gui-nightshift)

A collapsible "Cable Temperature" section on the Safety page: the enable
switch, then a pin-centric Input 1 (PP) / Input 2 (PP2) source picker (the 4
logical sources + None, cross-disabled between the two inputs so a source
can't be assigned to both), then per-source calibration fields and the live
reading once a source is picked. Assigned sources also get their own box on
the Monitoring Data tab. See `gui-nightshift/src/routes/settings/Safety.svelte`,
`gui-nightshift/src/lib/stores/cabletemp.js` and
`gui-nightshift/src/lib/cabletemp.js`.
