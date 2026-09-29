# Invariants — rules that must never break

Machine-checkable assertions about this repository. Any change that violates
one of these is wrong even if it compiles and tests pass. AI coding agents and
reviewers should verify each relevant item before proposing a change.

## Git / submodule

1. **The firmware's `gui-nightshift` pointer must always reference a commit
   that is pushed to the submodule's remote.** Push order is: submodule first,
   firmware pointer bump second. Check: `git submodule status` commit is
   reachable on `OpenEVSE/openevse-gui-nightshift`.
2. **A pointer bump commit must include the regenerated `src/web_static/`
   headers** built from the same `gui-nightshift/dist`. Check: `pio run -e
   openevse_wifi_v1` after `npm run build` produces no diff in `src/web_static/`.
3. All GUI work happens inside the `gui-nightshift/` submodule checkout — never
   edit `src/web_static/` by hand (it is generated).

## Configuration

4. **Every config option appears in exactly three places**: extern declaration
   in `src/app_config.h`, definition in `src/app_config.cpp`, and a
   `ConfigOptDefinition` entry in the `opts[]` array. Adding or removing an
   option touches all three.
5. **Changing a default in `app_config.cpp` requires updating the matching
   assertion in `divert_sim/test_config.py`** — that suite exists to catch
   accidental default changes.
6. Boolean config flags are bit positions in the `uint32_t flags` word; never
   reuse a bit.

## Firmware code patterns

7. **RAPI callbacks are always invoked, including on error paths.** Check
   `RAPI_RESPONSE_OK` before using results.
8. **Timeout comparisons use the signed-cast rollover pattern**
   `(long)(millis() - timeout) >= 0` — a plain `millis() > timeout` comparison
   breaks after the 49-day rollover.
9. Device writes and state changes go through `EvseManager` claims with the
   documented priorities (see [architecture](../developer/architecture.md)) —
   subsystems never command the controller directly.
10. The 4MB default env (`openevse_wifi_v1`) must keep fitting dual-slot OTA —
    stay on Arduino core 2.x / IDF4 for those boards; core-3-only APIs must be
    version-guarded (`ESP_ARDUINO_VERSION`).
11. Energy logging under `/logs/` has a hard 20 KB total budget on LittleFS —
    changes to `energy_logger` retention must respect it.

## Web UI (gui-nightshift)

12. Route components are the only store-aware units; pure logic lives in
    `src/lib/**` modules and is unit-tested.
13. Device writes are serialised through a single queue — the device's web
    server is single-threaded; never issue parallel config writes.
14. **A UI change that alters any screen's appearance regenerates the
    screenshots** (`npm run screenshots`) and commits the changed images with it.

## Validation gate — run after any change

```bash
cd gui-nightshift && npm run build && npm test && cd ..   # must pass
cd divert_sim && pytest -v && cd ..                       # must pass
git submodule status                                      # no unexpected state
```
