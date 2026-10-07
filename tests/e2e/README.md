# End-to-end UI tests

Gherkin scenarios, run with [Playwright](https://playwright.dev) through
[playwright-bdd](https://vitalets.github.io/playwright-bdd/), against the
**real firmware** and the **real web UI**:

```
Chromium ──HTTP/WS──▶ native firmware (pio -e native_openevse) ──RAPI/PTY──▶ OpenEVSE emulator
   (the user)            serves the embedded gui-nightshift UI            (charger hardware + the car)
```

A scenario describes what a user sees and does, and what the charger and the
car do in response. It checks both sides: the UI **and** the firmware's own
HTTP API / the emulator's view of the hardware, so a UI that merely *looks*
right but did not change the charger fails.

## Running

Prerequisites: Node 20+, Python 3.10+, `libavahi-client-dev` (native build),
a checkout of [OpenEVSE_Emulator](https://github.com/jeremypoulter/OpenEVSE_Emulator)
next to this repo (or `EMULATOR_DIR`) with `pip install -r requirements.txt`.

```bash
pio run -e native_openevse                      # once, and after firmware/UI changes
cd tests/e2e
npm install && npx playwright install chromium  # once
npm test                                        # everything
npm run test:smoke                              # just the @smoke scenarios
E2E_VERBOSE=1 npx playwright test -g "GFCI"     # one scenario, mirror device logs
npm run report                                  # open the HTML report
```

| Variable | Default | |
|---|---|---|
| `NATIVE_BINARY_PATH` | `.pio/build/native_openevse/program` | firmware binary |
| `EMULATOR_DIR` | `../OpenEVSE_Emulator` (sibling of this repo) | emulator checkout |
| `EMULATOR_PYTHON` | `python3` | interpreter with the emulator's requirements |
| `CHROMIUM_PATH` | Playwright's own download | use a system Chromium |
| `E2E_PORT_BASE` | `18000` | each worker uses a block of 10 ports |
| `E2E_VERBOSE` | unset | mirror firmware/emulator output to the terminal |

Firmware and emulator logs for each worker land in `output/worker-N/`.

## How it is organised

- `features/` — the scenarios, grouped by area. **This is the spec.** No
  selectors, URLs or JSON in here, only user-level language.
- `steps/` — step definitions (`common.steps.ts` for charger/car/backend,
  `ui.steps.ts` for the browser). Selectors and API calls live only here.
- `support/stack.ts` — starts one firmware + emulator pair per Playwright
  worker and gives every scenario a clean slate (fresh firmware config, emulator
  reset via `POST /api/test/reset`).
- `support/clients.ts` — thin REST clients for the emulator and the device.
- `coverage-exemptions.txt` — UI routes still without scenarios (a shrinking
  backlog), checked by `scripts/e2e_coverage.py`.

Every scenario starts with a brand new charger. Features other than the wizard
begin with `Given the charger has completed first-run setup`.

## Writing scenarios

```gherkin
@route:/
Feature: Safety faults
  Scenario: A GFCI trip stops charging and is reported
    Given a vehicle is plugged in and asks for charge
    And I have the dashboard open
    When the charger detects a GFCI trip
    Then the dashboard shows "GFCI fault"
    And the vehicle is not receiving a charge
```

- Tag the feature `@route:<ui route>` for the route(s) from
  `docs/ai/feature-map.md` it exercises (CI enforces that every route has one).
  Tag the handful of fast, critical scenarios `@smoke`.
- Reuse existing steps first (`grep "^Given\|^When\|^Then" steps/*.ts`).
  `Given`/`When`/`Then` are interchangeable, so one definition serves all three.
- Assert on the **backend as well as the screen**. Poll with `waitFor` /
  `expect.poll`; never `sleep`.
- Prefer role- and label-based locators (`getByRole('radio', { name: 'Off' })`).
  If the UI offers nothing stable to hook onto, add an `aria-label` or
  `data-testid` in `gui-nightshift` rather than a brittle CSS path.
- The emulator's `time_scale` (`Given the simulation runs N times faster than
  real time`) speeds up the **car and the emulated charger only**. The firmware
  meters energy against its own clock, so don't use it to fast-forward firmware
  energy counters.
- A scenario that fails is a bug in the firmware, the UI, the emulator or the
  scenario. Find out which. Do not skip, retry or quarantine it to get green.

## When something is missing from the emulator

Add it to the emulator repo (it has a `/api/test/*` control surface for exactly
this), document it in its `openapi.yaml`, then add a client method in
`support/clients.ts`. CI checks the emulator out from `EMULATOR_REF` in
`.github/workflows/e2e_tests.yaml`.
