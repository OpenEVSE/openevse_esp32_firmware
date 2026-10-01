# Crash Report Upload — Design

**Goal:** let a user hand a crash over in one click, and have a maintainer read
a **symbolized backtrace** rather than chase a binary blob and a matching ELF.

**Origin:** "May add a core dump upload tool they can click to grab them a hair
easier." — Chris: "A core dump tool would be fantastic."

**Status:** implemented (2026-09-28), then revised after review (2026-09-29 to
10-01). **The browser now sends the report, not the charger, and only the
decoded summary goes; read §15 first.** It supersedes the device-side flow in
§3, the heap tiers and deferral in §6, and D1, D3 and D4 in §14. The rest
stands as the reasoning behind the design.

---

## 1. What already exists

Nearly all the device-side machinery is in the tree. This design is mostly
wiring, plus infrastructure that does not exist yet.

| Piece | Where |
|---|---|
| Core dump, bounded at **64 KB** | `coredump` partition, `0x10000` in all three layouts (`min_spiffs.csv`, `min_spiffs_debug.csv`, `openevse_16mb.csv`) |
| **Zero-heap access to the dump** | `diagnostics_coredump_image()` returns a flash-mmapped pointer — the 64 KB never enters the heap |
| Decoded summary | `diagnostics_coredump_json()` — panic reason, faulting task, backtrace addresses, reset reason |
| Outbound HTTPS client | `MongooseHttpClient`, already used by `http_update.cpp` to pull firmware |
| TLS | mbedtls is **already linked** into `openevse_wifi_v1`; an HTTPS PUT adds single-digit KB, not ~100 |
| Trust store | `root_ca` bundle + `CertificateStore::getRootCa()` |
| Destination allowlisting | `http_update_url_allowed()` — the pattern §4 reuses |
| UI surface | `Terminal.svelte` already renders the summary, formats the backtrace, handles the RISC-V no-unwind case, links the raw image and guards an erase |

The gap is: no way to send it anywhere, and nothing that keeps the ELF needed to
read it.

## 2. Scope

**In:** a device-side uploader behind a new authenticated endpoint; a button in
Developer Tools; a CDK stack (broker, bucket, index, custom domain); CI
retention of `firmware.elf` keyed by version; server-side symbolization.

**Out:** uploading a crash nobody offered (§8); the dedicated upload boot mode,
documented as an escalation but deferred until measured (§6.3); surfacing
reports back to users; any change to how dumps are captured; OCPP or MQTT
transport.

## 3. Flow

One click, three device-side steps:

1. **`POST /debug/crash/upload`** on the device — authenticated like other
   config writes. Returns immediately; progress is reported over the existing
   websocket event channel.
2. **Device POSTs metadata to the broker.** Build identity, decoded summary,
   diagnostics, redacted config. Small JSON. Broker records it and replies
   `{report_id, upload_url}` where `upload_url` is a short-lived presigned PUT.
3. **Device PUTs the raw 64 KB** to `upload_url`, streamed straight from the
   mmapped pointer. Then a completion POST so the broker can mark the report
   whole and trigger symbolization.

Metadata and blob are split deliberately. The broker holds the searchable
index — which is the actual requirement, since maintainers are the ones
retrieving these — while the bucket holds bytes. A failed blob PUT still leaves
a report carrying a backtrace, which is usually enough.

The device never holds an AWS credential. It only ever sees an opaque,
expiring URL.

## 4. The destination is allowlisted, not configurable

A core dump is a RAM image: WiFi PSK, MQTT password, OCPP and Tesla tokens and
private key material can all be in it.

If the broker URL were an ordinary config key, an XSS or an authenticated but
hostile write could redirect every charger's memory image to an attacker.
Given this project's disclosure history that is a requirement, not a nicety:

- the broker host is **compiled in**, not a runtime config value;
- the same allowlist check `http_update_url_allowed()` already applies to OTA
  sources is applied to the broker and to the returned `upload_url`, so a
  compromised or spoofed broker response cannot redirect the blob off-domain;
- redirects are re-checked, as they are for OTA.

### Pin a stable domain, not the stack's generated hostname

The stack is CDK and will move accounts: RAR runs it initially, OpenEVSE takes
it over later. If the firmware allowlists an API Gateway hostname, that handover
breaks every already-flashed charger.

So the firmware pins a **stable custom domain** (`crash.openevse.com` or
similar), fronted by ACM and a custom domain in the stack. Handover is then a
DNS change, not a fleet reflash.

## 5. Payload

| Part | Contents | Carries secrets? |
|---|---|---|
| Build identity | `version`, `buildenv`, `chip_id`, `espinfo`, `firmware` | no |
| Decoded summary | panic reason, faulting task, backtrace addresses, reset reason | no |
| Diagnostics | `heap_largest`, `heap_largest_min`, `heap_min`, `free_heap`, `uptime`, LVGL pool peak | no |
| Redacted config | feature flags only — explicit allowlist of keys, never a blocklist | no |
| Raw dump | the 64 KB partition image | **yes** |

The redacted config is built from an **allowlist** of key names. A blocklist
would silently start leaking the next time someone adds a credential-bearing
config option.

`version` is the ELF lookup key (`local_<branch>_<hash>` locally, a release tag
from CI).

## 6. Heap, and when the upload runs

A TLS handshake needs tens of KB contiguous. `heap_largest` collapses to ~13 KB
after ~74 h uptime on a live unit — the documented reason OTA fails then and
succeeds after a reboot (see the heap-fragmentation work and
`ota-fails-under-connection-pressure`). A user will click this button on a
charger that has been up for weeks.

### 6.1 Three escalating windows

**Tier 1 — immediately on click.** Check `heap_largest` first; if it clears the
threshold, upload now. Most chargers, most of the time.

**Tier 2 — first network-connect after the next boot.** If tier 1 fails or the
heap check refuses, set a flag in NVS and tell the user it will go on the next
restart. On the next boot the flagged dump uploads at first-connect: before MQTT
connects, before OCPP, before tsdb writes, before the web UI has served
anything. This automates what would otherwise be "reboot and retry" advice, and
does not force a reboot on a charger that may be mid-charge.

**Tier 3 — a dedicated upload boot mode.** Documented here as the escalation
path, **not built initially** (§6.3). Boot, see the flag, bring up only WiFi and
the uploader — no RAPI, LVGL, scheduler, divert, OCPP, MQTT or web server —
upload against essentially the whole heap, then reboot normally.

### 6.2 Why startup is not simply re-ordered

`net.begin()` is second-to-last in `setup()` and `Mongoose.begin()` follows it.
It could move earlier — it needs LittleFS and config, and must follow
`lcd.begin()` because net calls back into `lcd.setWifiMode()` — but the
obstacle is time, not order. Association and DHCP take seconds. Uploading into a
pristine heap would mean **blocking `setup()` until the network is up**, which
stalls RAPI, the display and everything else for seconds, or indefinitely if
WiFi is down or the AP has moved. Blocking boot on network availability is not
acceptable on a device whose job is to keep servicing a car. Hence tiers, not
re-ordering.

### 6.3 Tier 3 is deferred until measurement justifies it

There is no evidence yet that tier 2 is insufficient. The 13 KB figure is from a
different workload; this payload is 64 KB streamed from flash, so the only real
consumer is the handshake. The bench S3 measured flat — 46 h cost 2 KB of
contiguous heap (65,524 → 63,476) — but it runs a fake controller, so it says
nothing about RAPI-driven fragmentation on the boards that matter.

The measurement needs a controller-attached unit and is blocked on the garage
EVSE replacement (see `tft-unit-addresses`). It is about half an hour's work
once that lands.

If tier 3 does prove necessary, the NVS flag from tier 2 already exists and the
upgrade is contained — no change to the device API or the broker contract.
Constraints it would have to meet, recorded now so they are not rediscovered:

- **Clear the flag before attempting, not after.** A crash mid-upload must not
  boot back into upload mode; that is a loop on a device that is not charging.
- **Hard timeout (~60 s), then reboot normally regardless.**
- **ESP-side features are absent while it runs.** The ATmega/SAMD controller is
  autonomous and holds the real safety interlocks, so a car keeps charging — but
  **temperature throttling is ESP-side** (`Priority_Safety` derate) and would not
  apply. That is the argument for a tight timeout rather than a generous one.
- **Needs a manual trigger** so a rarely-reached boot path can be tested rather
  than only exercised after a panic.

### 6.4 The upload must not block loopTask

Whichever tier runs it, the upload is asynchronous, like the existing OTA
client. A synchronous call on the connect path is the same shape as the MQTT DNS
bug that tripped the 5 s task WDT and became upstream #1252.

## 7. Erase policy

The dump is erased **only** after a 2xx from the bucket *and* a successful
completion POST. Anything else leaves it in place. A dump erased on a partial
upload is unrecoverable, and the existing manual erase already covers the case
where a user wants it gone.

## 8. Consent

**Nothing uploads that a user did not click.** That is the invariant, and the
tiers in §6 are all downstream of one click.

Tier 2 needs stating carefully, because it completes on a boot where nobody
pressed anything: the *consent* happened at the click, and the NVS flag is only
a deferred completion of that single authorised action. It is set only by a
click, cleared once consumed, and never re-armed by the firmware. A dump that
was never offered for upload is never uploaded — and a user who changes their
mind can clear the flag, or erase the dump outright with the control that
already exists.

What is **not** in this design is uploading a crash nobody offered. That is a
separate decision for Chris to make explicitly rather than have inferred,
because it means credential-bearing memory images leaving devices with nobody
present. The device endpoint and broker contract are shaped so such a caller
could use them unchanged later, so excluding it now costs no rework.

The confirm dialog states plainly that this is a memory image and may contain
the WiFi password and other credentials — same shape as the existing erase
confirmation. When the click defers to tier 2, the UI says so explicitly rather
than reporting a silent success: the user should know the upload has not
happened yet and what will make it happen.

## 9. The CDK stack

| Resource | Purpose |
|---|---|
| HTTP API + Lambda | broker: records metadata, mints the presigned PUT, triggers symbolization |
| S3 bucket (private) | reports and ELFs; block-public-access on; **90-day lifecycle rule** |
| Index table | report list keyed by `report_id`, searchable by version / chip / date |
| ACM cert + custom domain | the stable hostname §4 requires |
| Symbolizer Lambda | runs `esp-coredump` against the matching ELF, stores the result beside the report |

Sized for tens of reports a day; comfortably inside free tier. Written so
`cdk deploy` into a different account is the whole handover.

## 10. ELF archive and symbolization

This is the piece that makes the rest worth building. A report reading
`version: local_perf/lvgl-own-task_7b0829d5` is undecodable unless something
kept that exact ELF — the hard-won rule already recorded as *"ALWAYS archive
firmware.elf per flash or the backtrace is unsymbolizable"*.

- **CI uploads `firmware.elf` per env per build**, keyed by the same `version`
  string the device reports (~53 MB each). **ELFs must outlive reports**: a
  report arriving on day 89 of a build's life is useless if its ELF expires on
  day 90. So ELF retention is longer than the report lifecycle, not equal to
  it — §13 settles the figure.
- **On completion the symbolizer Lambda** looks up the ELF by `version`, runs
  `esp-coredump`, and stores a symbolized backtrace next to the report.
- **When no ELF matches** — a local developer build — the report is kept and
  flagged unsymbolized. It still carries the summary, and a developer can
  symbolize locally against their own archived ELF.

The outcome this design is aiming at: a maintainer opens a report and reads a
stack trace, without asking the user for anything.

## 11. Flash cost and gating

`openevse_wifi_v1` is at **90.0%** and `esp32-c3-devkitc-02` at **90.4%** of
their app partitions. Reusing mbedtls and `MongooseHttpClient` should keep the
uploader small, but the figure is **measured before it is claimed**, and the
feature ships behind a `DISABLE_*` opt-out in the established style so the tight
boards can drop it.

## 12. Risks

| Risk | Handling |
|---|---|
| Dump contains credentials | §4 allowlist, §8 consent copy, §9 90-day lifecycle, private bucket |
| Upload fails on a long-uptime charger | §6 tiered windows: heap check, then deferred to next boot's first-connect |
| Tier 2 completes on an unattended boot | §8 flag is set only by a click, consumed once, never self-armed |
| Tier 3 boot mode loops, or charges untended | §6.3 clear-flag-before-attempt, hard timeout, temp-throttle caveat |
| Stack moves accounts, fleet breaks | §4 stable custom domain |
| Reports arrive undecodable | §10 CI ELF retention keyed by the reported version |
| 4 MB boards overflow | §11 measured cost, opt-out gate |
| Redaction drifts as config grows | §5 allowlist, not blocklist |

## 13. Open questions

- Exact `heap_largest` threshold for §6.1 tier 1, and whether tier 2 suffices or
  tier 3 is needed. **Blocked on the garage EVSE replacement** — the only
  controller-attached board available, and the bench S3's fake controller cannot
  reproduce RAPI-driven fragmentation.
- How much longer ELF retention should be than the 90-day report lifecycle
  (§10). It has to exceed it; the figure depends on how long a build stays in
  the field, which release cadence decides.
- Whether the broker should rate-limit per chip id to bound a misbehaving or
  hostile device.

## 14. As built

Four deliberate departures, each recorded in the implementation plan
(`docs/superpowers/plans/2026-09-28-crash-report-upload.md`):

- **D1 — the raw dump goes to the broker, not a presigned S3 URL** (§3). A
  presigned URL would bake the bucket name and region into every charger,
  breaking the account handover §4 exists for. The broker's own domain carries
  `PUT /v1/reports/{id}/raw` instead; the device still holds no AWS
  credential, only the report's UUID.
- **D2 — completion marks the report whole; it does not run `esp-coredump`**
  (§10). Symbolization is `addr2line` on the reported backtrace at metadata
  time. The raw image is kept for a maintainer to decode locally.
- **D3 — tier 2 starts from `loop()` at first connectivity** (§6.1). It races
  MQTT and OCPP rather than strictly preceding them; a hook inside the connect
  path is where upstream #1252 came from.
- **D4 — only the heap gate defers; a tier-1 failure mid-flight does not**
  (§6.1). The device cannot tell heap starvation from a DNS, network or broker
  failure, and deferring those behind "not enough memory, will send after
  restart" would misinform. A failure keeps the dump and the user can retry.

Also as built: the deferred flag is a LittleFS file, not NVS (§6.1) — nothing
else on the device writes an arbitrary LittleFS path, so only the endpoint can
arm it. It stores the offered dump's identity (SHA-256 prefix + length), so a
crash after the click is never sent in its place (§8). The identity cannot be a
CRC32: a core dump ends with its own CRC32, which makes the whole-image CRC32 a
constant for every valid dump. The client follows no redirects at all (§4),
which is stricter than re-checking them. When the dump predates an OTA the
report's `version` is `unknown`, with `running_version` alongside.

Measured cost on `openevse_wifi_v1_16mb`: +8,572 bytes of flash, +128 bytes of
RAM. On for the three 16 MB envs, off elsewhere (§11).

## 15. Revisions after review

Three changes, each from review on the PRs. Together they move the sending
off the charger.

### R1 — the decoded summary only, never the raw dump (2026-09-28)

The raw image is a copy of RAM. Config redaction (§5) never reaches what is in
memory, so Wi-Fi and MQTT credentials can be in it (review on #1307). The
summary alone names every frame, because symbolization is `addr2line` on the
backtrace (D2). Released firmware sends no image. The broker's `raw` and
`complete` routes (D1) remain, but nothing released calls them.

### R2 — a random reporter id in place of the chip id, and erasure (2026-09-29)

The chip id is MAC-derived: personal data under GDPR. Only about 24 bits of it
are unknown within Espressif's OUIs, so even a hash of it brute-forces back
quickly. Reports instead carry a random **reporter id** (16 bytes) and the
SHA-256 of a random **delete key** (32 bytes), never the key itself. Presenting
the key to `POST /v1/reporters/{id}/delete` erases every report filed under
that id: GDPR Art. 17, and Art. 7(3) (withdrawing consent as easy as giving
it). The broker answers `200 {deleted: n}` either way, so a guessed id learns
nothing. The by-chip index was dropped, and `chip_id` was stripped from the
rows already stored. After an erasure the id is discarded, so later reports
are not linked to the erased ones.

### R3 — the browser sends, not the charger (2026-09-30, review on #1306)

**Design.** The charger only builds the report and keeps the identity. The
GUI, in the user's browser, does all the network work:

- **Send:** read the identity → if none, generate one (below) and store it →
  `GET /debug/crash/report` → `POST` it to the broker with `fetch()` → on
  200, `DELETE /debug/crash`.
- **Delete:** `GET /debug/crash/identity?key=1` → `POST` the key to the
  broker's delete route → on 200, `DELETE /debug/crash/identity`.

**Charger routes** (all behind `requestPreProcess`):

| Route | Purpose |
|---|---|
| `GET /debug/crash/report` | The report exactly as the broker takes it. 404 with no dump, 409 with no identity, 500 if it would not fit (never sent truncated). |
| `GET /debug/crash/identity` | The reporter id and broker URL. The delete key only with `?key=1`, which the GUI asks for only from Delete. |
| `POST /debug/crash/identity` | Store the identity the browser generated. Same → 200, different → 409 (a second tab carries on with the stored one), malformed → 400. |
| `DELETE /debug/crash/identity` | Forget the identity, once the reports are erased. |

**What this removes.** The on-device TLS client, upload and delete state
machines, the heap gate, and deferral to the next boot (§6). One TLS request
cost a no-PSRAM board about 30 KB of its largest free block, which is what
forced the gate and the deferral. It also removes the compiled-in host
allowlist (§4). The broker URL is still a build constant
(`CRASH_BROKER_URL`), but now the charger hands it to the GUI rather than
dialling it. The charger needs no internet access; the browser does.

**Decisions inside R3:**

- **The browser generates the identity,** with `crypto.getRandomValues()`.
  That is a CSPRNG and works on plain-HTTP pages; `crypto.subtle` does not. The
  ESP32's RNG is only truly random with the radio on, an Ethernet charger may
  have Wi-Fi off, and `bootloader_random_enable()` must not run while Wi-Fi is
  up.
- **Every identity route requires `X-Requested-With: OpenEVSE`.** With no
  password set there is no auth and so no cookie CSRF check. A cross-site
  `text/plain` form could otherwise POST a JSON-shaped body and plant an
  identity, which the charger would then keep, letting that site erase
  everything sent afterwards. A cross-site form cannot set the header.
- **The broker allows CORS from `*`,** POST only, `content-type` only, no
  credentials. The charger is served from any address, and no broker route
  takes a cookie or credential.
- **Partial failures are said as such.** A report sent but not erased says so,
  rather than "removed", which would invite a duplicate. Reports erased but
  the id kept says so, and keeps Delete: a reused id would link later reports
  to the erased ones.

**Validated** on a no-PSRAM WROOM, in headless Chromium: the report was sent
and symbolized, then deleted (`{deleted: 1}`), and the key was fetched once, at
deletion. PRs: firmware #1306, GUI openevse-gui-nightshift#157, broker
openevse-crash-service#8.

