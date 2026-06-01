# Home Assistant Vehicle-Data Source — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user pick Home Assistant as the charger's vehicle-data source and configure HA entity IDs for SoC/range/ETA, which the HA client polls (via the existing `homeAssistant.get()` seam) and feeds into the EVSE manager.

**Architecture:** Approach A — the poll lives inside `HomeAssistantClient`, driven by its existing 30 s `loop()`, gated on `isConnected() && vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT`. A pure, natively-tested `ha_parse_entity_state()` extracts `.state` from HA's `/api/states/<entity>` JSON; the poll chains the configured entities sequentially on the shared HTTP client and calls `evse.setVehicleStateOfCharge/Range/Eta`.

**Tech Stack:** C++ (Arduino-ESP32, PlatformIO), ArduinoMongoose (`MongooseHttpClient`), ArduinoJson 6.20.1, doctest (native tests), Svelte + vitest (gui-nightshift).

**Spec:** `docs/superpowers/specs/2026-06-01-ha-vehicle-data-source-design.md`
**Branch:** `feature/home-assistant-oauth` (this work continues on it — it depends on the HA-connection code that isn't merged to master yet).

**Build/test commands** (plain `pio` for native/core-2; `./scripts/pio` for the P4/core-3 env):
- Native unit tests: `pio test -e native -f test_ha_oauth`
- Core-2 compile check: `pio run -e nodemcu-32s`
- P4 (gui bundled): `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4`

**GUI clone note:** Do the gui work in the **in-tree** clone `gui-nightshift/` (the firmware build bundles its `dist`). Publishing to GitHub happens from the canonical clone `/home/rar/openevse-gui-nightshift` in the final task (the in-tree clone's remote is HTTPS / no push creds; the classifier blocks changing it).

---

## File Structure

**Firmware (this repo):**
- Modify `src/ha_oauth.h` / `src/ha_oauth.cpp` — add pure `ha_parse_entity_state()`.
- Modify `test/test_ha_oauth/test_ha_oauth.cpp` — doctest cases for it.
- Modify `src/app_config.h` — append `VEHICLE_DATA_SRC_HOMEASSISTANT`; extern the 3 config strings.
- Modify `src/app_config.cpp` — declare + register the 3 config strings.
- Modify `src/home_assistant.h` / `src/home_assistant.cpp` — vehicle polling (loop gate + `pollVehicle()`/`pollVehicleField()`, `_lastVehiclePoll`, `evse`/`app_config` use).

**GUI (`gui-nightshift/`, in-tree):**
- Modify `src/routes/settings/Vehicle.svelte` — HA source option + entity fields.
- Modify `src/routes/settings/__tests__/Vehicle.test.js` — reveal test.
- Modify the 4 locale files `src/lib/i18n/{en,es,fr,hu}.json` — new vehicle strings.

---

## Task 1: Pure `ha_parse_entity_state()` + native tests

**Files:**
- Modify: `src/ha_oauth.h`, `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

- [ ] **Step 1: Declare the function in `src/ha_oauth.h`**

Add after the `ha_parse_token_response(...)` declaration:

```cpp
// Extract the `.state` string from a Home Assistant /api/states/<entity> JSON
// response. Returns false if the body doesn't parse, `state` is missing/non-string,
// or it is empty / "unknown" / "unavailable". Uses an ArduinoJson filter so it is
// memory-safe regardless of how large the entity's `attributes` object is.
bool ha_parse_entity_state(const std::string &json, std::string &out);
```

- [ ] **Step 2: Add failing tests** — append to `test/test_ha_oauth/test_ha_oauth.cpp`:

```cpp
TEST_CASE("ha_parse_entity_state extracts the state, ignoring attributes") {
  std::string s;
  CHECK(ha_parse_entity_state(
      "{\"entity_id\":\"sensor.car\",\"state\":\"73.5\","
      "\"attributes\":{\"unit_of_measurement\":\"%\",\"friendly_name\":\"Car\"}}", s));
  CHECK(s == "73.5");
}

TEST_CASE("ha_parse_entity_state rejects unavailable/unknown/missing/bad") {
  std::string s;
  CHECK_FALSE(ha_parse_entity_state("{\"state\":\"unavailable\"}", s));
  CHECK_FALSE(ha_parse_entity_state("{\"state\":\"unknown\"}", s));
  CHECK_FALSE(ha_parse_entity_state("{\"state\":\"\"}", s));
  CHECK_FALSE(ha_parse_entity_state("{\"attributes\":{}}", s)); // no state key
  CHECK_FALSE(ha_parse_entity_state("not json", s));
}
```

- [ ] **Step 3: Run to verify failure**

Run: `pio test -e native -f test_ha_oauth`
Expected: FAIL — undefined reference to `ha_parse_entity_state`.

- [ ] **Step 4: Implement in `src/ha_oauth.cpp`** (append; `<ArduinoJson.h>` is already included from the token-parser task):

```cpp
bool ha_parse_entity_state(const std::string &json, std::string &out) {
  // Filter so only "state" is materialized — entity `attributes` can be large.
  StaticJsonDocument<32> filter;
  filter["state"] = true;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) {
    return false;
  }
  if (!doc["state"].is<const char *>()) {
    return false;
  }
  std::string s = doc["state"].as<const char *>();
  if (s.empty() || s == "unknown" || s == "unavailable") {
    return false;
  }
  out = s;
  return true;
}
```

- [ ] **Step 5: Run to verify pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS (all prior cases + 2 new).

- [ ] **Step 6: Commit**

```bash
git add src/ha_oauth.h src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "feat(ha): parse entity .state from /api/states response

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Config — enum value + entity-ID fields

**Files:**
- Modify: `src/app_config.h`, `src/app_config.cpp`

No unit test (config glue); verified by the Task 3 build.

- [ ] **Step 1: Append the enum value in `src/app_config.h`**

Change the `vehicle_data_src` enum to append HA last (preserving existing numeric values):

```cpp
enum vehicle_data_src {
  VEHICLE_DATA_SRC_NONE,
  VEHICLE_DATA_SRC_TESLA,
  VEHICLE_DATA_SRC_MQTT,
  VEHICLE_DATA_SRC_HTTP,
  VEHICLE_DATA_SRC_HOMEASSISTANT
};
```

- [ ] **Step 2: Add externs in `src/app_config.h`**

Next to the other Home Assistant externs (`extern String ha_url;` … `extern String ha_client_id;`), add:

```cpp
extern String ha_vehicle_soc;
extern String ha_vehicle_range;
extern String ha_vehicle_eta;
```

- [ ] **Step 3: Declare the variables in `src/app_config.cpp`**

After the existing Home Assistant variable block (`String ha_client_id;`), add:

```cpp
String ha_vehicle_soc;
String ha_vehicle_range;
String ha_vehicle_eta;
```

- [ ] **Step 4: Register the ConfigOpt entries in `src/app_config.cpp`**

After the `new ConfigOptDefinition<String>(ha_client_id, "", "ha_client_id", "hac"),` line, add:

```cpp
  new ConfigOptDefinition<String>(ha_vehicle_soc, "", "ha_vehicle_soc", "hvs"),
  new ConfigOptDefinition<String>(ha_vehicle_range, "", "ha_vehicle_range", "hvr"),
  new ConfigOptDefinition<String>(ha_vehicle_eta, "", "ha_vehicle_eta", "hve"),
```

- [ ] **Step 5: Verify it compiles (built with Task 3)**

These vars are consumed in Task 3; do not commit separately. Mark this task's checkboxes done and proceed to Task 3, which builds and commits both together.

---

## Task 3: Vehicle polling in `HomeAssistantClient`

**Files:**
- Modify: `src/home_assistant.h`, `src/home_assistant.cpp`

- [ ] **Step 1: Add the poll-interval macro in `src/home_assistant.cpp`**

Near the other `HA_*` macros at the top (after `HA_LOOP_INTERVAL_MS`), add:

```cpp
#define HA_VEHICLE_POLL_MS        (30 * 1000UL)   // poll HA vehicle entities every 30 s
```

- [ ] **Step 2: Add includes in `src/home_assistant.cpp`**

With the other includes at the top, add:

```cpp
#include <math.h>      // lround
#include "input.h"     // global EvseManager `evse`
```

(`app_config.h` — which declares `vehicle_data_src` and the `ha_vehicle_*` strings — is already included.)

- [ ] **Step 3: Declare members + methods in `src/home_assistant.h`**

In the `private:` section, after `unsigned long _lastRefreshAttempt;` add:

```cpp
    unsigned long _lastVehiclePoll;
```

After the `void storeTokens(const HaTokens &t);` line add:

```cpp
    void pollVehicle();
    void pollVehicleField(int field); // 0=soc, 1=range, 2=eta; chains to the next
```

- [ ] **Step 4: Initialize the member in the constructor**

In `src/home_assistant.cpp`, the constructor initializer list currently ends with `_lastRefreshAttempt(0)`. Change it to also init the new member:

```cpp
HomeAssistantClient::HomeAssistantClient() :
  MicroTasks::Task(),
  _pendingStateTime(0),
  _refreshInFlight(false),
  _lastRefreshAttempt(0),
  _lastVehiclePoll(0)
{
}
```

- [ ] **Step 5: Add the poll gate to `loop()`**

In `src/home_assistant.cpp`, in `loop()`, immediately BEFORE the final `return HA_LOOP_INTERVAL_MS;`, add:

```cpp
  // Poll HA vehicle entities when HA is the selected vehicle-data source.
  if (isConnected()
      && vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT
      && (_lastVehiclePoll == 0 || (millis() - _lastVehiclePoll) >= HA_VEHICLE_POLL_MS)) {
    _lastVehiclePoll = millis();
    if (_lastVehiclePoll == 0) _lastVehiclePoll = 1; // 0 means "never polled"
    pollVehicle();
  }
```

- [ ] **Step 6: Implement `pollVehicle()` / `pollVehicleField()`**

Append to `src/home_assistant.cpp`:

```cpp
void HomeAssistantClient::pollVehicle() {
  pollVehicleField(0);
}

// Fetch one configured vehicle entity, set the matching EVSE value, then chain to
// the next field. Sequential (one in-flight request at a time) to stay safe on the
// shared MongooseHttpClient. An empty entity, a failed request, or an
// unknown/unavailable state simply skips that field (keeps the last good value).
void HomeAssistantClient::pollVehicleField(int field) {
  String entity;
  switch (field) {
    case 0: entity = ha_vehicle_soc;   break;
    case 1: entity = ha_vehicle_range; break;
    case 2: entity = ha_vehicle_eta;   break;
    default: return; // chain complete
  }

  if (entity.length() == 0) {
    pollVehicleField(field + 1); // not configured -> skip to next
    return;
  }

  String path = "/api/states/" + entity; // entity IDs are URL-safe (sensor.x_y)
  int next = field + 1;
  get(path, [this, field, next](MongooseHttpClientResponse *response) {
    if (response->respCode() == 200) {
      MongooseString body = response->body();
      std::string state;
      if (ha_parse_entity_state(std::string((const char *)body, body.length()), state)) {
        double v = atof(state.c_str());
        switch (field) {
          case 0: evse.setVehicleStateOfCharge((int)lround(v)); break;
          case 1: evse.setVehicleRange((int)lround(v));         break;
          case 2: evse.setVehicleEta((int)v);                   break; // seconds
        }
      } else {
        DBUGF("[ha] vehicle field %d: state unavailable/unparseable", field);
      }
    } else {
      DBUGF("[ha] vehicle field %d: HTTP %d", field, response->respCode());
    }
    pollVehicleField(next); // advance regardless of this field's outcome
  });
}
```

- [ ] **Step 7: Build (this also validates Task 2)**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 8: Native tests still pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 9: Commit (Tasks 2 + 3 together)**

```bash
git add src/app_config.h src/app_config.cpp src/home_assistant.h src/home_assistant.cpp
git commit -m "feat(ha): poll HA entities for vehicle SoC/range/ETA

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: gui-nightshift — Vehicle settings HA source

**Files (in `gui-nightshift/`, in-tree):**
- Modify: `src/routes/settings/Vehicle.svelte`
- Test: `src/routes/settings/__tests__/Vehicle.test.js`
- Modify: `src/lib/i18n/en.json`, `es.json`, `fr.json`, `hu.json`

Work from `/home/rar/oevse/openevse_esp32_firmware/gui-nightshift`. Commit in that repo.

- [ ] **Step 1: Read the current files**

Read `src/routes/settings/Vehicle.svelte` (note the `srcOptions` array and the `{#if src === 2}` MQTT block — the HA block mirrors it for `src === 4`), `src/routes/settings/__tests__/Vehicle.test.js` (mirror its mock/render setup), and the `config.vehicle` block in `src/lib/i18n/en.json` (note the exact existing keys: `src_none/src_tesla/src_mqtt/src_http`, `topic_soc/topic_range/topic_eta`, `range_unit`).

- [ ] **Step 2: Write the failing test** — add this case to the `describe('Vehicle page', ...)` block in `src/routes/settings/__tests__/Vehicle.test.js` (it mirrors the existing MQTT/HTTP cases exactly — `config_store.set(...)` then assert on the i18n key, since `svelte-i18n` is mocked to return keys):

```js
  it('reveals HA entity fields when the source is Home Assistant', () => {
    config_store.set({ vehicle_data_src: 4 })
    const { getByText, queryByText } = render(Vehicle)
    expect(getByText('config.vehicle.entity_soc')).toBeInTheDocument()
    expect(queryByText('config.vehicle.topic_soc')).not.toBeInTheDocument() // not the MQTT block
  })
```

- [ ] **Step 3: Run to verify failure**

Run (in `gui-nightshift/`): `npm test -- Vehicle`
Expected: FAIL — no element with text `config.vehicle.entity_soc`.

- [ ] **Step 4: Add the HA source option in `Vehicle.svelte`**

In the `srcOptions` `$derived` array, add a 5th entry:

```js
    { value: '4', label: $_('config.vehicle.src_homeassistant') },
```

- [ ] **Step 5: Add the HA reveal block in `Vehicle.svelte`**

Immediately AFTER the `{:else if src === 3} … {/if}` HTTP block's content and BEFORE the closing `{/if}`, add a new branch (i.e. change the `{:else if src === 3}` block so the HA branch follows it):

```svelte
  {:else if src === 4}
    <ConfigSection title={$_('config.vehicle.src_homeassistant')}>
      <p class="text-sm text-text-dim">{$_('config.vehicle.ha_info')}</p>
      <FormField label={$_('config.vehicle.range_unit')} status={$ss.mqtt_vehicle_range_miles ?? 'idle'}>
        <Select
          options={unitOptions}
          value={String(!!$config_store?.mqtt_vehicle_range_miles)}
          onchange={(v) => form.saveField('mqtt_vehicle_range_miles', v === 'true')}
        />
      </FormField>
      <FormField label={$_('config.vehicle.entity_soc')} status={$ss.ha_vehicle_soc ?? 'idle'}>
        <TextInput
          value={$config_store?.ha_vehicle_soc ?? ''}
          placeholder="sensor.car_battery"
          revert={form.revert}
          onchange={(v) => form.saveField('ha_vehicle_soc', v)}
        />
      </FormField>
      <FormField label={$_('config.vehicle.entity_range')} status={$ss.ha_vehicle_range ?? 'idle'}>
        <TextInput
          value={$config_store?.ha_vehicle_range ?? ''}
          placeholder="sensor.car_range"
          revert={form.revert}
          onchange={(v) => form.saveField('ha_vehicle_range', v)}
        />
      </FormField>
      <FormField label={$_('config.vehicle.entity_eta')} status={$ss.ha_vehicle_eta ?? 'idle'}>
        <TextInput
          value={$config_store?.ha_vehicle_eta ?? ''}
          placeholder="sensor.car_time_to_full"
          revert={form.revert}
          onchange={(v) => form.saveField('ha_vehicle_eta', v)}
        />
      </FormField>
    </ConfigSection>
  {/if}
```

(The `mqtt_vehicle_range_miles` range-unit field is reused — it's the EVSE's single range-unit setting, per the spec.)

- [ ] **Step 6: Add i18n strings to ALL four locales**

In the `config.vehicle` object of `src/lib/i18n/en.json`, add:

```json
      "src_homeassistant": "Home Assistant",
      "ha_info": "Reads vehicle data from Home Assistant entities. Requires Home Assistant to be connected (Settings → Home Assistant).",
      "entity_soc": "SoC entity",
      "entity_range": "Range entity",
      "entity_eta": "Time-to-full entity",
```

Add the same keys to `es.json`, `fr.json`, `hu.json` (translate the values; keep the key set identical across all four — there is a locale-parity test). Suggested translations:
- es: `"src_homeassistant": "Home Assistant"`, `"ha_info": "Lee los datos del vehículo desde entidades de Home Assistant. Requiere que Home Assistant esté conectado (Ajustes → Home Assistant)."`, `"entity_soc": "Entidad de SoC"`, `"entity_range": "Entidad de autonomía"`, `"entity_eta": "Entidad de tiempo de carga"`.
- fr: `"src_homeassistant": "Home Assistant"`, `"ha_info": "Lit les données du véhicule depuis des entités Home Assistant. Nécessite que Home Assistant soit connecté (Réglages → Home Assistant)."`, `"entity_soc": "Entité de SoC"`, `"entity_range": "Entité d'autonomie"`, `"entity_eta": "Entité de temps de charge"`.
- hu: `"src_homeassistant": "Home Assistant"`, `"ha_info": "A jármű adatait Home Assistant entitásokból olvassa. Csatlakoztatott Home Assistant szükséges (Beállítások → Home Assistant)."`, `"entity_soc": "SoC entitás"`, `"entity_range": "Hatótáv entitás"`, `"entity_eta": "Töltési idő entitás"`.

- [ ] **Step 7: Run the Vehicle test + full suite**

Run: `npm test -- Vehicle`
Expected: PASS.

Run: `npm test`
Expected: PASS (no regressions; locale-parity test green).

- [ ] **Step 8: Build the GUI**

Run: `npm run build`
Expected: SUCCESS.

- [ ] **Step 9: Commit (in gui-nightshift)**

```bash
cd gui-nightshift
git add src/routes/settings/Vehicle.svelte src/routes/settings/__tests__/Vehicle.test.js src/lib/i18n/
git commit -m "feat(ha): Home Assistant vehicle-data source in Vehicle settings

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
cd ..
```

---

## Task 5: Full build + hardware validation + publish GUI

**Files:** none (verification + publish)

- [ ] **Step 1: Native tests**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 2: Core-2 regression build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 3: P4 build with GUI bundled**

Run: `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4`
Expected: SUCCESS.

- [ ] **Step 4: Flash + on-hardware validation (manual)**

Flash the P4 (`GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5`), wait for HTTP 200 at the board IP, then with HA connected:
- Settings → Vehicle → set source = **Home Assistant**; set `ha_vehicle_soc` to a real HA `sensor.*` reporting a battery %.
- Confirm `GET /status` shows `battery_level` matching the entity, and the value appears on the display.
- Set range/ETA entities; confirm `battery_range` updates.
- Point an entity at an `unavailable` sensor; confirm the last good value is retained (no clobber, no crash).

- [ ] **Step 5: Commit any validation fixes; verify the firmware branch is clean**

Commit any fixes found, then confirm `git status` shows only the known deliberately-excluded leftovers.

- [ ] **Step 6: Publish the GUI from the canonical clone**

The firmware build bundles the in-tree `gui-nightshift`; publishing happens from `/home/rar/openevse-gui-nightshift` (SSH remote). Transplant the Task 4 commit and push:

```bash
S=/home/rar/oevse/openevse_esp32_firmware/gui-nightshift
T=/home/rar/openevse-gui-nightshift
git -C "$T" fetch origin && git -C "$T" checkout main && git -C "$T" rebase origin/main
rm -rf /tmp/ha-veh-patch && mkdir -p /tmp/ha-veh-patch
git -C "$S" format-patch -1 HEAD -o /tmp/ha-veh-patch          # the Task 4 gui commit
git -C "$T" am /tmp/ha-veh-patch/*.patch
git -C "$T" push origin main
```

If `am` conflicts (e.g. the canonical clone's Vehicle.svelte differs), resolve by re-applying the Task 4 edits onto the canonical clone's Vehicle.svelte, then `git -C "$T" am --continue`. NOTE: pushing from `$T` is allowed (its remote is already SSH); changing the in-tree clone's remote is NOT (classifier-blocked) — always publish from `$T`.

- [ ] **Step 7: Push the firmware branch**

```bash
git push origin feature/home-assistant-oauth
```

---

## Notes for the implementer

- **Async chaining:** `pollVehicleField()` re-enters itself from the `get()` `onResponse` lambda (or directly when an entity is unconfigured). `homeAssistant` is a program-lifetime global, so the `[this]` capture is safe (same basis as the existing exchange/refresh callbacks). Only one request is ever in flight.
- **`get()` gating:** `homeAssistant.get()` already returns early unless `isConnected() && ha_url` set, so the poll is inert when HA isn't connected — the `loop()` gate is belt-and-suspenders.
- **Range units:** reuse the existing `mqtt_vehicle_range_miles` flag (the EVSE's one range-unit setting); do not add a new units config.
- **No `main.cpp` change:** unlike Tesla (pumped from `main`), the HA client's own `loop()` drives the poll.
- **Two gui clones:** build/bundle from in-tree `gui-nightshift`; push from `/home/rar/openevse-gui-nightshift` (see Task 5 Step 6).
