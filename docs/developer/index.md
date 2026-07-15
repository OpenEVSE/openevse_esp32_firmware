# Developer guide

- [Architecture](architecture.md) — subsystems, the EvseManager
  client/priority system, RAPI patterns, configuration management, energy
  logging, coding conventions
- [Building the firmware](building.md) — PlatformIO envs, the two-core tree,
  debug builds, host-side tests, divert simulator
- [Web UI development](gui-development.md) — gui-nightshift workflow, mock
  mode, automated screenshots, the submodule embed rules
- [Wired Ethernet](../wired-ethernet.md) — Olimex ESP32-Gateway support

API references: [HTTP](https://openevse.stoplight.io/docs/openevse-wifi-v4/)
([api.yml](../../api.yml)) · [MQTT](../mqtt.md)
([developer guide](../Developers_Guide_MQTT.md)) · [RAPI](../rapi.md)

AI coding-agent context: [AGENTS.md](../../AGENTS.md),
[invariants](../ai/invariants.md), [feature map](../ai/feature-map.md).

## Change management

Bugs and change requests go through the
[GitHub issue tracker](https://github.com/OpenEVSE/openevse_esp32_firmware/issues) —
one issue per bug/feature. All changes are made on a branch or fork and
submitted as a pull request; PRs are reviewed by a repository administrator
before merge (administrators' own PRs are reviewed by another administrator).

CI must be green: firmware builds for 15+ board envs (`build.yaml`), the GUI
build (`build_gui.yml`), the divert simulator suite (`divert_sim.yaml`), and
OpenAPI validation (`openapi_validate.yaml`).

## Creating a release

1. Ensure GitHub Actions are complete and green.
2. Create a new [GitHub release](https://github.com/OpenEVSE/openevse_esp32_firmware/releases/new)
   with a [semver](https://semver.org/) tag, e.g. `v5.1.0`.
3. Add release notes (start from the auto-generated notes and edit).
4. Tick `Pre-release` and publish — the build workflow attaches the binaries.
5. Test the uploaded binaries, at minimum: `openevse_wifi_v1.bin`,
   `olimex_esp32-gateway-e.bin`, `adafruit_huzzah32.bin`.
6. Untick `Pre-release`, update the release, and confirm the release
   validation action is green.
