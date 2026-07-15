# OpenEVSE ESP32 Firmware

**The canonical AI-agent guidance for this repository is [`AGENTS.md`](../AGENTS.md)
at the repo root** — build/bootstrap commands, the validation gate, and the
critical workflows. Read it first. Deeper context:

- [docs/ai/invariants.md](../docs/ai/invariants.md) — rules that must never break
- [docs/ai/feature-map.md](../docs/ai/feature-map.md) — feature → source → config → API → docs
- [docs/developer/architecture.md](../docs/developer/architecture.md) — subsystem map and patterns

Essentials, duplicated here for quick reference:

- The default web UI is the **`gui-nightshift`** submodule (not `gui-v2`).
  Build it before the firmware: `git submodule update --init --recursive &&
  cd gui-nightshift && npm install && npm run build`.
- `pio run -e openevse_wifi_v1` builds the default board. First build downloads
  ~500 MB and takes 15–45 minutes — **never cancel**.
- `src/web_static/` is generated from `gui-nightshift/dist` — never edit by
  hand; regenerate and commit it with any GUI submodule pointer bump, and only
  bump to submodule commits that are already pushed.
- After any change: `cd gui-nightshift && npm run build && npm test`, and
  `cd divert_sim && pytest -v` must pass.
- When a PR changes any user-visible screen (`gui-nightshift`, `gui-v2`,
  `gui-tft`, `src/lvgl_tft/`), include fresh screenshots of each changed screen
  — for gui-nightshift run `npm run screenshots` and commit the regenerated
  images.
