# Crash Report Upload — Design

**Goal:** let a user hand a crash over in one click, and have a maintainer read
a **symbolized backtrace** rather than chase a binary blob and a matching ELF.

**Origin:** "May add a core dump upload tool they can click to grab them a hair
easier." — Chris: "A core dump tool would be fantastic."

**Status:** design, not yet approved for implementation.

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

**Out:** automatic/unattended upload (§8); surfacing reports back to users;
any change to how dumps are captured; OCPP or MQTT transport.

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

## 6. Heap is the failure mode that will actually bite

A TLS handshake needs tens of KB contiguous. `heap_largest` collapses to ~13 KB
after ~74 h uptime — the documented reason OTA fails then and succeeds after a
reboot (see the heap-fragmentation work and `ota-fails-under-connection-pressure`).

A user will click this button on a charger that has been up for weeks.

So the endpoint **checks `heap_largest` before starting** and refuses with a
clear "reboot and retry" rather than thrashing a failing handshake. That advice
works because the dump survives reboots. The threshold is measured on the bench
board, not guessed.

## 7. Erase policy

The dump is erased **only** after a 2xx from the bucket *and* a successful
completion POST. Anything else leaves it in place. A dump erased on a partial
upload is unrecoverable, and the existing manual erase already covers the case
where a user wants it gone.

## 8. Consent

Click-only for now. No automatic or unattended upload in this design — that is a
separate decision, and one Chris should make explicitly rather than have
inferred, because it means credential-bearing memory images leaving devices
without anyone present.

The confirm dialog states plainly that this is a memory image and may contain
the WiFi password and other credentials — same shape as the existing erase
confirmation. The device endpoint and broker contract are shaped so an automatic
caller could use them unchanged later, so choosing click-only now costs no
rework.

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
| Upload fails on a long-uptime charger | §6 pre-flight heap check with actionable advice |
| Stack moves accounts, fleet breaks | §4 stable custom domain |
| Reports arrive undecodable | §10 CI ELF retention keyed by the reported version |
| 4 MB boards overflow | §11 measured cost, opt-out gate |
| Redaction drifts as config grows | §5 allowlist, not blocklist |

## 13. Open questions

- Exact `heap_largest` threshold for §6 — measure on the bench S3 and a WROOM.
- How much longer ELF retention should be than the 90-day report lifecycle
  (§10). It has to exceed it; the figure depends on how long a build stays in
  the field, which release cadence decides.
- Whether the broker should rate-limit per chip id to bound a misbehaving or
  hostile device.
