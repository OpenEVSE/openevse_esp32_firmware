# Handoff: Notification Advisories — GUI half

**For:** whoever builds the web-UI side, in `OpenEVSE/openevse-gui-nightshift` (branch `RePartition`).

**From:** the firmware half, complete on `feature/notifications` in `openevse_esp32_firmware` (28 commits off upstream master `af0c7649`). Code-complete, reviewed, builds on both envs, 15/15 native suites. **Not yet hardware-verified** — treat the API shapes below as authoritative (they are read from the source, not the spec), but expect the firmware branch to move if bench testing finds something.

**Read alongside:** `docs/superpowers/specs/2026-09-11-notification-advisories-design.md` in the firmware repo — §4.1, §9 and §10 are the ones that constrain your work.

---

## 1. What this feature is, in one paragraph

Advisories are conditions the owner should know about that are **not faults**: safety checks switched off, faults that have already cleared, thermal state, relay wear. They sit in a tier *below* the full-screen fault page (which means "this charger has stopped") and *above* the event log (history you go looking for). The firmware already draws an amber border and one line on the LCD. Your job is the half that makes them actionable.

**The failure mode that matters is crying wolf.** If the UI nags about something the owner has deliberately acknowledged, they stop believing it, and it is worth nothing when it matters. Every design decision below follows from that.

---

## 2. API

### `GET /notifications`

```json
{
  "count": 2,
  "max_severity": "critical",
  "notifications": [
    {
      "id": "safety.ground_check",
      "category": "safety",
      "severity": "critical",
      "sticky": true,
      "acked": false,
      "first_seen": 1757548800,
      "last_seen": 1757552400
    }
  ]
}
```

- `count` and `max_severity` are **unmuted only** — they are the badge numbers.
- The `notifications` array lists **everything, muted included**, each with its own `acked`. A muted item must stay visible in your list, marked muted; it must not vanish.
- Consequence to handle: you can legitimately receive `count: 0` alongside a non-empty array. That is not a bug.
- `severity` ∈ `"info" | "warning" | "critical"`. `category` ∈ `"safety" | "fault" | "wear" | "thermal"`.
- `first_seen` / `last_seen` are **epoch seconds**, and **`0` means "clock was not yet synced when this was recorded"** — render that as unknown, not as 1970.

### `POST /notifications/ack?id=<id>`

Both `?id=` on the query string and a form-encoded body work. `GET` works too but needs the `X-Requested-With: OpenEVSE` header (the CSRF guard shared with `/divertmode`, `/apoff` et al.).

| Response | Meaning |
|---|---|
| 200 | acked |
| 400 | no `id` supplied |
| 404 | that id is not currently live |
| 403 | headerless cross-site GET |

### `/status` — exactly two fields

```json
"notifications": { "count": 2, "severity": "critical" }
```

Deliberately minimal; the list lives on its own endpoint. `/status` is polled hard by the HA integration, so please don't ask for more here.

### Websocket

An event fires **only when the set changes**, carrying the same two fields:

```json
{ "notifications": { "count": 2, "severity": "critical" } }
```

Seed from `/status`, merge websocket deltas, and re-fetch `/notifications` when `count` or `severity` moves. Note there is **no** per-item push — the list itself only arrives via the endpoint.

### Event log

`/logs/<n>` rows may now carry `"type": "notification"` and `"notification": "<id>"`. The field is **absent** on older rows and on every non-notification row — treat absence as normal, not as an error.

---

## 3. The 16 advisories

`id` is the only thing you key on. Titles and detail text are yours to write and translate; the firmware never sends prose.

| id | category | severity | sticky |
|---|---|---|---|
| `safety.ground_check` | safety | critical | yes |
| `safety.gfci_check` | safety | critical | yes |
| `safety.relay_check` | safety | warning | yes |
| `safety.diode_check` | safety | warning | yes |
| `safety.vent_check` | safety | warning | yes |
| `safety.temp_check` | safety | warning | yes |
| `fault.gfci_tripped` | fault | critical | no |
| `fault.no_ground` | fault | critical | no |
| `fault.stuck_relay` | fault | critical | no |
| `thermal.throttling` | thermal | warning | yes |
| `thermal.high_temp` | thermal | warning | yes |
| `thermal.relay_thermal` | thermal | info → warning | yes |
| `wear.relay_life` | wear | info → warning | no |
| `wear.relay_transit_drift` | wear | warning | yes |
| `wear.relay_cold_open` | wear | warning | no |
| `wear.stuck_relay_recovery` | wear | info | no |

Two ids change severity with condition (`wear.relay_life` at ≤20% / ≤5% remaining; `thermal.relay_thermal` at watch / warn), so **don't hard-code severity per id** — read it from the payload.

All `wear.*` and `thermal.relay_*` advisories are **absent entirely** on a controller without the RELAY_HEALTH feature. Absent means "this charger can't report it", not "it's fine" — don't render a zero state for them.

---

## 4. Sticky vs ackable — the bit most likely to be got wrong

**`sticky: true`** (all six safety checks, thermal, drift): acking **mutes**, it does not clear.

| | not acked | acked (muted) |
|---|---|---|
| badge `count` / `severity` | counted | **not** counted |
| LCD border and line | driven | not driven |
| your notification list | listed | listed, marked muted |
| **your settings-page inline marker** | shown | **still shown** |

Nothing is ever hidden — only the alarm is silenced. That last row is the point: the owner who muted "ground check is off" still sees it beside the switch, so the state is never secret.

**`sticky: false`** (fault counters, relay life, cold opens, recoveries): acking dismisses until the underlying value moves. A later GFI trip re-raises it automatically; you don't need to do anything.

Acks persist across reboots and are invalidated by the firmware on a settings change, a counter change, or a firmware update — so an item you acked can legitimately come back unacked. Don't cache `acked` locally across sessions; trust the payload.

---

## 5. What to build, in the order that matters

1. **Inline markers on the settings page** — a warning marker beside the very switch that is off, drawn from the advisory list, **not suppressed by muting**. This is the one that makes the whole feature useful: "ground check is off" is only actionable if it is one tap from the control that fixes it. If you build only one thing, build this.
2. **A strip on the status page** for `critical` advisories, dismissible.
3. **A bell badge in the header** opening a panel listing everything newest-first, with ack buttons and links through to the relevant setting or to Monitoring → Health.

The bell is the index, not the feature. A badge that only opens a list of complaints the user must then go hunting for is a worse version of the event log they already have.

**Do not restate relay numbers.** Monitoring → Health already renders life remaining, cold opens, electrical damage, transit drift, thermal index and recovery count. A `wear.*` advisory's job is to tell someone who isn't on that page that they should be — link there, don't duplicate the figure.

**Colour:** amber for warning, red only for `critical`. The firmware reserves red for the fault page, and the LCD uses amber for everything; keeping the web UI close to that keeps the two tiers legible as different things.

---

## 6. Mock / dev server

`npm run dev:mock` in gui-nightshift serves the whole UI against `dev/fixtures/`. **The notification feature is not in those fixtures yet** — `/api/status` has no `notifications` object and `/api/notifications` falls through to the SPA index.

To develop against it you'll want, in `dev/mock-plugin.js` and `dev/fixtures/`:

- `notifications` added to `status.json` (`{"count": 2, "severity": "critical"}`)
- a `notifications.json` fixture — worth including a realistic mix: a muted sticky safety item, an unacked critical, a `wear.relay_life` at info, and one with `first_seen: 0` so the unknown-timestamp path gets exercised
- `/api/notifications` wired into the fixture map
- a `POST /api/notifications/ack` handler that flips `acked` and recomputes `count` / `max_severity`
- optionally a `notifications` scenario beside the existing `display` / `wizard` ones

Ask if you'd like the firmware side to supply those — it's a small job and it keeps the fixture honest against the real payloads.

Real device to test against: the firmware branch flashes to any TFT unit, but as of this handoff no hardware has run it yet.

---

## 7. Things that are settled, so please don't re-litigate

- `/status` carries two fields and no more.
- The list includes muted items; the badge count excludes them.
- Ids are stable and locale-independent — all display text is yours.
- Muting never hides the settings-page marker.
- Timestamps are epoch with `0` = unknown.

## 8. Open questions you may hit

- **Detail copy for all 16 ids** is unwritten. The LCD has 16 short upper-case strings (`notification_short_text()` in `src/notifications.cpp`) you can crib tone from, but they are deliberately terse and carry no numbers.
- **HA entities** are out of scope for now, but the ids are stable specifically so a binary sensor per advisory stays possible later. Don't do anything that would require them to become dynamic.
