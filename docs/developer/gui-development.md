# Web UI development

The default web UI is **gui-nightshift** — a Svelte 5 + Vite + Tailwind app in
the `gui-nightshift/` git submodule ([repository](https://github.com/OpenEVSE/openevse-gui-nightshift)).
It is a pure client of the device's HTTP + WebSocket API; the production build
is a small gzipped static bundle embedded into the firmware image.

`gui-v2` is the previous UI, kept as a submodule for reference; select it with
`GUI_NAME=gui-v2` at firmware build time.

## Development workflow

```bash
cd gui-nightshift
npm install
npm run dev:mock     # offline against built-in mock data — no hardware needed
npm run dev          # against a real charger (set VITE_OPENEVSEHOST in .env)
npm test             # vitest unit tests
npm run build        # production build → dist/
```

Mock mode serves canned fixtures for every API endpoint and a fake WebSocket,
including runtime switchers for EVSE state and named fixture scenarios — see
the [gui-nightshift README](../../gui-nightshift/README.md) for details. A
`docker-compose.yml` in the submodule spins up the UI against the native
firmware build and a device emulator for full end-to-end development.

## Screenshots

All UI screenshots in the documentation are generated automatically:

```bash
cd gui-nightshift
npm run screenshots   # regenerates docs/screenshots/*.png deterministically
```

The capture manifest is `gui-nightshift/scripts/screenshots.config.js`. When a
UI change alters a screen's appearance, regenerate and commit the images in the
same change.

## Embedding into the firmware — the submodule rules

The firmware build embeds `gui-nightshift/dist` as C headers into
`src/web_static/` (via `scripts/extra_script.py`). Those generated headers are
**tracked** and must be committed whenever the GUI changes.

The full cycle for a GUI change:

```bash
cd gui-nightshift
# ...edit src/..., then:
npm run build                    # produces dist/
git add -A && git commit -m "feat(...): ..."
git push                         # push the submodule FIRST
cd ..
pio run -e openevse_wifi_v1      # regenerates src/web_static/ from dist/
git add gui-nightshift src/web_static
git commit -m "chore: bump gui-nightshift"   # pointer bump + regenerated assets
git push
```

Two rules that must never be broken:

1. **Never commit a firmware pointer bump for a submodule commit that hasn't
   been pushed** — anyone cloning the firmware would get an unresolvable
   submodule reference. Push order is always: submodule first, firmware second.
2. **`src/web_static/` must be regenerated and committed together with the
   pointer bump**, so the embedded UI always matches the recorded submodule
   commit.
