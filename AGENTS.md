# AGENTS.md — working on the OpenEVSE ESP32 firmware

Guidance for AI coding agents (and a quick orientation for humans). Deeper
references: [docs/developer/architecture.md](docs/developer/architecture.md),
[docs/ai/invariants.md](docs/ai/invariants.md),
[docs/ai/feature-map.md](docs/ai/feature-map.md).

## What this is

ESP32 WiFi gateway firmware for OpenEVSE charging stations. It talks RAPI
(serial) to an ATmega/SAMD controller and provides the web UI, HTTP/WebSocket
API, MQTT, solar divert, OCPP 1.6, scheduling, and energy logging. The web UI
is the **`gui-nightshift` git submodule** (Svelte 5) — `gui-v2` is the legacy
UI, only used if explicitly selected.

## Bootstrap and build

```bash
git submodule update --init --recursive
cd gui-nightshift && npm install && npm run build && cd ..
pio run -e openevse_wifi_v1        # default 4MB board
```

First firmware build downloads ~500 MB of toolchain and takes 15–45 minutes —
**never cancel it**; incremental builds take 2–5 minutes. Full env list and the
two-core (IDF4/IDF5) subtleties: [docs/developer/building.md](docs/developer/building.md).

## Sandbox

Agent work can run behind Claude Code's built-in Bash sandbox — an egress allowlist plus
filesystem/credential guards. That part is **opt-in and per-developer**: paste the starting
point from [docs/ai/sandbox.md](docs/ai/sandbox.md) into your gitignored
`.claude/settings.local.json`. On Linux/WSL2 first `sudo apt-get install bubblewrap socat`,
and on Ubuntu 24.04+ add the `bwrap` AppArmor profile or every command fails with a
`nested userns` error (especially in the VS Code extension). The `/sandbox` panel (terminal
CLI only) shows the resolved policy. With it on, build/test commands run without prompts;
`docker` (integration tests) runs outside the sandbox.

What *is* checked in — and applies whether or not you sandbox — is
[`.claude/settings.json`](.claude/settings.json): read denials for `*.pem`, `.env*` and
`.vscode/settings.json`, and a confirmation prompt on `git push` and OTA flash.

## Test

```bash
cd gui-nightshift && npm test && cd ..        # UI unit tests (vitest)
cd divert_sim && pip install -r requirements.txt && pytest -v && cd ..
pio test -e native_test                       # host-side firmware unit tests
```

[`scripts/openevse_test.sh`](scripts/openevse_test.sh) wraps all of the above
plus the emulator-backed harness, and is the path of least friction for agents:

```bash
scripts/openevse_test.sh all                  # unit + divert + gui
scripts/openevse_test.sh native -n 2          # 2 firmware instances, status dump
scripts/openevse_test.sh native -- curl -s "$EVSE_URL/status"
scripts/openevse_test.sh emulator -n 2        # 2 firmware + emulator pairs (needs docker)
scripts/openevse_test.sh integration          # emulator-backed pytest (needs docker)
scripts/openevse_test.sh launch -i 1          # one long-lived emulator + firmware pair
scripts/openevse_test.sh launch -i 1 --pr 1027   # ... running a PR's build, not yours
```

Each sandboxed Bash call gets its **own network namespace**, so a server started
in one command is unreachable from the next. The wrapper therefore starts,
uses, and tears down everything inside a single invocation — pass what you want
to run after `--`, and read the instance URLs from `$EVSE_URL` / `$EVSE_URL_<i>`
(also `$EVSE_NAME_<i>`, `$EVSE_NAMES`, `$EVSE_COUNT`).
`emulator` and `integration` need Docker and so run outside the sandbox.

Every instance gets its own hostname (`openevse-ev<i>`) and chip id, so several
are addressable at once — needed for the load sharing work in
[openevse_esp32_firmware#1027](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1027),
where members join a group by mDNS name.

`launch` is for humans and REST clients: it brings up a single pair for one
instance ID (ports, chip ID, hostname and working directory all derived from the
ID, all individually overridable) and stays in the foreground until Ctrl-C, so a
REST client such as `test/*.http` can drive it. The emulator comes from the
Docker image by default; `--local` runs it from a source checkout and
`--no-emulator` gives a firmware-only instance that works inside the sandbox.
The firmware console is mirrored to the terminal (`[fw<i>] …`) and the emulator
console is not — `--[no-]firmware-console` / `--[no-]emulator-console` flip either.

The firmware is your local native build unless you ask otherwise. `--pr N` runs a PR's
published image instead (`--firmware-tag` / `--firmware-image` for any other), which is the
quick way to try someone's change without building it. Fork PRs have no published image —
their CI cannot push — and `--pr` says so if you hit one.

## Validation gate — run after ANY change

```bash
cd gui-nightshift && npm run build && npm test && cd ..
cd divert_sim && pytest -v && cd ..
git submodule status        # must show a clean, pushed state
```

## Critical workflows

### GUI change → firmware embed (submodule — strict order)

`src/web_static/` holds **generated** headers built from `gui-nightshift/dist`;
they are tracked and must never be edited by hand. After a GUI change:

1. In `gui-nightshift/`: `npm run build`, commit, **push the submodule**.
2. In the firmware repo: `pio run -e openevse_wifi_v1` (regenerates
   `src/web_static/`), then commit the submodule pointer bump **together with**
   the regenerated headers, and push.

Never bump the pointer to a submodule commit that isn't pushed.

### UI screenshots

If a UI change alters any screen, regenerate the documentation screenshots in
the same change: `cd gui-nightshift && npm run screenshots` (deterministic;
manifest in `scripts/screenshots.config.js`), then mirror them into the user
guide with `python scripts/sync_screenshots.py` (CI enforces the sync).

### Config options

Every option lives in exactly three places: extern in `src/app_config.h`,
definition in `src/app_config.cpp`, and a `ConfigOptDefinition` in the `opts[]`
array. Changing a **default** also requires updating the assertion in
`divert_sim/test_config.py`. Update
[docs/ai/feature-map.md](docs/ai/feature-map.md) and the relevant
`docs/user/` page when adding options — `python scripts/docs_coverage.py
--strict` (run by CI) fails on undocumented options, routes, or API paths.

## Code conventions (essentials)

- `snake_case` config vars, `_snake_case` privates, `PascalCase` classes,
  `UPPER_SNAKE_CASE` constants.
- Timeouts: `(long)(millis() - timeout) >= 0` (survives 49-day rollover).
- RAPI: async with lambda callbacks; check `RAPI_RESPONSE_OK`; **always invoke
  the callback, including on error paths**.
- EVSE state changes go through `EvseManager` claims (priority table in
  [architecture.md](docs/developer/architecture.md)) — never command the
  controller directly from a subsystem.
- Debug output via `DBUGLN()`/`DBUGF()`/`DBUGVAR()` inside `#ifdef ENABLE_DEBUG`.

The full list of must-not-break rules: [docs/ai/invariants.md](docs/ai/invariants.md).
