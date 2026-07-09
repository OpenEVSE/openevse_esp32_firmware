# OpenEVSE WiFi documentation

Documentation is organised by audience.

## I want to use my charger — [User guide](user/index.md)

Setting up WiFi, charging modes, solar divert, schedules, integrations
(Home Assistant, MQTT, EmonCMS), and troubleshooting. One page per screen of
the web UI, with screenshots.

## I want to modify the firmware or UI — [Developer guide](developer/index.md)

- [Architecture](developer/architecture.md) — subsystems, EvseManager
  priorities, RAPI patterns, config management
- [Building the firmware](developer/building.md) — PlatformIO envs, the
  two-core tree, host tests
- [Web UI development](developer/gui-development.md) — gui-nightshift workflow,
  mock mode, screenshots, submodule rules
- [Wired Ethernet](wired-ethernet.md)

## API references

- [HTTP API](https://openevse.stoplight.io/docs/openevse-wifi-v4/) (OpenAPI
  source: [api.yml](../api.yml))
- [MQTT API](mqtt.md) and the [MQTT developer guide](Developers_Guide_MQTT.md)
- [RAPI protocol](rapi.md)

## AI / coding-agent context — [docs/ai/](ai/)

- [`AGENTS.md`](../AGENTS.md) (repo root) — build/test commands, validation
  gate, critical workflows
- [Invariants](ai/invariants.md) — rules that must never break
- [Feature map](ai/feature-map.md) — feature → source → config → API → docs
