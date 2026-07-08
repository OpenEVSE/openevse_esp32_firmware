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

## Test

```bash
cd gui-nightshift && npm test && cd ..        # UI unit tests (vitest)
cd divert_sim && pip install -r requirements.txt && pytest -v && cd ..
pio test -e native_test                       # host-side firmware unit tests
```

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
