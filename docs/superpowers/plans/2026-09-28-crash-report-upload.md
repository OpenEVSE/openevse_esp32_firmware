# Crash Report Upload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a user clicks one button and a maintainer gets a symbolized crash report plus the raw memory image, without either of them touching a serial cable.

**Architecture:** the device POSTs a small JSON metadata document to a broker on a compiled-in hostname, gets back a `report_id`, PUTs the 64 KB core dump straight out of its flash mapping to `/v1/reports/{id}/raw` on that same host, then POSTs a completion. The broker is the Lambda already deployed for Plan A, extended with two routes; the index row it already writes grows a `raw` field. Nothing new is credentialed on the device: the `report_id` is an unguessable UUID and is the only capability it holds.

**Tech Stack:** ESP32 Arduino/PlatformIO + mongoose 6.18 (ArduinoMongoose), AWS CDK (TypeScript) + Python 3.12 Lambda, Svelte 5 (gui-nightshift).

**Spec:** `docs/superpowers/specs/2026-09-27-crash-report-upload-design.md`

**Predecessor:** `docs/superpowers/plans/2026-09-27-crash-elf-archive-symbolization.md` (Plan A) is **built and deployed**. Its bucket, index table, symbolizer Lambda and `POST /v1/reports` route exist in AWS account `669423882102`, region `us-east-2`. This plan extends that stack; it does not rebuild it.

---

## Global Constraints

Every task's requirements implicitly include this section.

- **The broker host is compiled in, never a config value.** `CRASH_BROKER_HOST` defaults to `crash.openevse.com`; it is overridable only by a `-D` build flag, never by `/config`, NVS or LittleFS. (Spec §4)
- **Every outbound URL passes a host allowlist.** Spec §4 also says redirects are re-checked; this client **follows no redirects at all** — any 3xx is a failure — which satisfies the requirement more strictly than re-checking would. Stated here so it is not mistaken for an omission.
- **Redaction is an allowlist of config key names, never a blocklist.** (Spec §5)
- **Nothing uploads that a user did not click.** The deferred flag is set only by the device endpoint, consumed once, and never re-armed by firmware. (Spec §8)
- **The dump is erased only after a 2xx on the raw PUT *and* a 2xx on the completion POST.** Anything else leaves it in place. (Spec §7)
- **The upload never blocks `loopTask`.** No synchronous network call on any path this plan touches. (Spec §6.4)
- **Enabled on 16 MB envs only**, via `-DENABLE_CRASH_UPLOAD=1`. Default off, so a new 4 MB env cannot silently overflow. Flash cost is **measured before it is claimed**. (Spec §11)
- Timeouts use `(long)(millis() - deadline) >= 0` so they survive the 49-day rollover.
- Commit as `git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" commit`. No AI attribution in commit messages.
- **Never commit regenerated `src/web_static/`.** GUI work is a separate repo and a separate PR; the submodule pointer bump is not part of this plan.
- Firmware builds go through `scripts/pio`, never a bare `pio`, so a concurrent build's PlatformIO core directory is not disturbed.
- Node/npm via `bash -lc 'source ~/.nvm/nvm.sh; nvm use 22 >/dev/null; ...'`.

## Deviations from the spec

Three, all decided here with reasons. Each is a ruling the executor inherits; do not re-litigate them mid-task.

**D1 — the raw dump is PUT to the broker, not to a presigned S3 URL.** Spec §3 says the broker mints a presigned PUT and the device uploads straight to S3.

The reason is **account portability, not the allowlist**. A presigned URL lives on `<bucket>.s3.<region>.amazonaws.com`, and that exact hostname could be compiled in and allowlisted just as the broker's is — so "we would have to allowlist all of S3" is wrong and is not the argument to rely on. What a presigned URL would really cost is the property spec §4 exists for: the bucket name and region would be baked into every flashed charger, so moving the stack to OpenEVSE's account would break the fleet exactly as pinning the API Gateway hostname would. Routing the 64 KB through the broker keeps **one** name in the firmware, costs one Lambda invocation well inside the 10 MB HTTP API payload cap, and leaves "the device never holds an AWS credential" intact. §3 of the spec should be amended to match.

It does move the abuse surface, and Task 4 bounds it: the metadata POST is unauthenticated by design, so without a per-route limit anyone can declare `raw_bytes: 131072` and push 128 KB per report at the stage's 10 rps — roughly 110 GB a day, retained 90 days. The raw route therefore gets its own, much tighter throttle. Spec §13's per-chip limit stays open.

**D2 — the completion POST marks the report whole; it does not run `esp-coredump`.** Spec §10 describes symbolization via `esp-coredump` against the archived ELF. Plan A shipped `addr2line` against the reported backtrace and it works, so the symbolized trace already exists by the time the raw dump arrives. Packaging `esp-coredump` and its ELF-tools dependency into the Lambda is a separate piece of work with its own failure modes, and the raw dump's value in the meantime is that a maintainer can download it and run `esp-coredump` locally against an ELF the archive now guarantees exists — which is today's workflow minus the hard part. Out of scope here; the completion route is shaped so it can trigger it later without a device-side change.

**D3 — tier 2 runs from `loop()` at first connectivity, not from a hook inside the connect path.** Spec §6.1 says "before MQTT connects, before OCPP, before tsdb writes". Reaching that literally means a new callback in `net_manager`, which widens the blast radius of this change into the code path that the MQTT-DNS watchdog bug (#1252) came from. Polling `net.isConnected()` from `crash_upload_loop()` starts the upload at the first moment it *can* run and races the other subsystems' own connect schedules rather than strictly preceding them. That is weaker than the spec's wording and is stated as such in the docs task. Tier 3 (spec §6.3) is the real answer and remains deferred.

## Facts established before writing this plan

Verified against the tree and the deployed stack; the tasks below depend on them.

1. **`MongooseHttpClient` cannot carry the raw dump.** `MongooseHttpClient::send()` passes the body to `mg_connect_http_opt()` as `(const char *)`, and mongoose 6.18 does `strlen(post_data)` and picks the method with `post_data[0] == '\0' ? "GET" : "POST"` (`mongoose.c:8716-8721`). So a body with an embedded NUL truncates, `setMethod(HTTP_PUT)` is silently ignored, and `mg_printf` copies the whole payload into the connection's send buffer — undoing the zero-heap flash mapping. The raw PUT therefore drives a mongoose connection directly (Task 5).
2. **Outbound HTTPS in this firmware does not verify certificates today.** `MongooseHttpClient::send()` calls `Mongoose.getDefaultOpts(&opts)` with `secure` defaulted to `false`, so `ssl_ca_cert` stays NULL and mongoose substitutes `"*"` — "faux-SSL with no verification" (`mongoose.c:8672`). This plan's uploader passes `secure = true` and gets the real bundle. **The OTA download path's exposure is out of scope here and must be raised separately.**
3. **Amazon Root CA 1 is already in the bundle** (`src/root_ca.cpp`, `CA_AMAZON_1`), so an ACM certificate on `crash.openevse.com` validates with no trust-store change.
4. **`diagnostics_coredump_image()` returns a flash-mmapped pointer valid for the life of the boot** (`src/diagnostics.h`), so the 64 KB never enters the heap and the pointer may outlive a request handler.
5. **The existing index's `chip_id` is the *controller's* chip id**, not the ESP's — `scripts/symbolize_crash.py` reads `config.chip_id`, which is `evse.getChipId()` (`app_config.cpp:1082`). Every unit with a fake or absent controller collapses to one value, making the `by-chip` GSI useless. Task 3 repoints it at `ESPAL.getLongId()` (`serial`, `main.cpp:153`). The index currently holds three rows of test data, so the change costs nothing.
6. **16 MB envs in this repo's master are `openevse_wifi_tft_v1`, `openevse_wifi_tft_v1_dev` and `openevse_wifi_v1_16mb`** (`platformio.ini`). S3 and P4 envs live on other branches and are out of scope.
7. **The deployed API is `https://ci84ymdemf.execute-api.us-east-2.amazonaws.com/v1/reports`**; bucket `crashstorage-crashbucketed041c25-mulnvmsdrfn4`; account `669423882102`, region `us-east-2`.

## Review Focus

Input classes the spec implies that no task's tests exercise by default. Each has a test pinned to the task that owns the code; a reviewer checks each deliberately.

1. **A hostile or compromised broker reply.** The metadata response is attacker-controlled once the broker is. A `report_id` containing `../`, a newline, or 4 KB of text must not become a request path — Task 5 pins this.
2. **The dump erased or overwritten mid-upload.** `diagnostics_coredump_erase()` can be called from the web API while an upload is in flight; the mapping must not be torn down under the sender. Task 6 pins this.
3. **A second click while an upload is running.** Two concurrent uploaders sharing one mapping and one state struct is a double-free waiting to happen. Task 6 pins it by refusing.
4. **The broker accepting a raw PUT for a report that does not exist, is already complete, or is stale.** An unauthenticated write endpoint keyed by a UUID must still check the UUID names a live report. Task 2 pins all three.
5. **A raw body that is not 64 KB.** Zero-length, 10 MB, or a body that is not the coredump partition size at all. Task 2 pins the bounds.

## File Structure

**Service — `/home/rar/oevse/openevse-crash-service`** (repo `git@github.com:RAR/openevse-crash-service.git`, branch off `main`)

| File | Responsibility |
|---|---|
| `lambda/symbolize/handler.py` (modify) | route dispatch; the existing metadata POST grows an `upload` block |
| `lambda/symbolize/raw.py` (create) | raw-dump PUT: decode, bound, store, mark |
| `lambda/symbolize/complete.py` (create) | completion POST: mark whole, return final status |
| `lambda/symbolize/test_raw.py` (create) | bounds, staleness, replay, missing report |
| `lambda/symbolize/test_complete.py` (create) | completion semantics |
| `lambda/symbolize/test_handler.py` (modify) | metadata response now carries `upload` |
| `lib/symbolize-stack.ts` (modify) | two new routes, `s3:PutObject` on `reports/*`, table read+update |
| `lib/domain-stack.ts` (create) | ACM cert + API Gateway custom domain + optional Route53 alias |
| `bin/openevse-crash-service.ts` (modify) | wire the domain stack, conditionally |
| `test/domain-stack.test.ts` (create) | the three configuration cases |
| `README.md` (rewrite) | it is still `cdk init` boilerplate |

**Firmware — `/home/rar/oevse/openevse_esp32_firmware`** (branch `feat/crash-upload` off `oe-ssh/master`)

| File | Responsibility |
|---|---|
| `src/crash_upload.h` / `.cpp` (create) | the whole uploader: state machine, three requests, tier gates |
| `src/crash_payload.h` / `.cpp` (create) | build the metadata JSON; the redaction allowlist lives here, alone |
| `src/ota_url_allow.h` / `.cpp` (modify) | add `crash_url_host_allowed()` beside the OTA one, sharing the parser |
| `src/web_server.cpp` (modify) | `POST /debug/crash/upload`, `GET /debug/crash/upload` |
| `src/main.cpp` (modify) | `crash_upload_begin()` in setup, `crash_upload_loop()` in loop |
| `platformio.ini` (modify) | `-DENABLE_CRASH_UPLOAD=1` on the three 16 MB envs |
| `test/test_crash_url_allow/*` (create) | host allowlist, on the build host |
| `test/test_crash_payload/*` (create) | redaction allowlist, on the build host |
| `docs/user/troubleshooting.md`, `docs/ai/feature-map.md` (modify) | the new endpoint and the consent copy |

**GUI — `gui-nightshift` submodule** (separate repo, separate PR)

| File | Responsibility |
|---|---|
| `src/routes/settings/Terminal.svelte` (modify) | the button, the confirm dialog, progress, deferred copy |
| `src/lib/i18n/{en,es,fr,hu}.json` (modify) | the strings |

---

### Task 1: The metadata POST hands out an upload ticket

**Repo:** `/home/rar/oevse/openevse-crash-service`, branch `feat/upload-broker` off `main`.

`main` has an uncommitted change in the working tree (the create-or-reference OIDC work in `lib/symbolize-stack.ts` and its test). Commit that first, or Task 4's diff lands on top of it and the review package conflates the two.

**Files:**
- Modify: `lambda/symbolize/handler.py`
- Modify: `lambda/symbolize/test_handler.py`

**Interfaces:**
- Consumes: the existing `handler(event, context)` and its DynamoDB item shape (Plan A).
- Produces: index items gain `raw` (`'none' | 'expected' | 'stored'`), `raw_bytes` (int) and `complete` (bool). The response gains an optional `upload` object `{"path": str, "expires_in": int}`. Tasks 2 and 3 read `raw`, `raw_bytes` and `created_at`.

The device tells the broker up front how many bytes it intends to send. That lets the bounds check happen before 64 KB crosses the wire, and it makes the index row honest about whether a dump is still coming rather than inferring it from an absence.

- [ ] **Step 1: Write the failing tests**

Append to `lambda/symbolize/test_handler.py`:

```python
def test_a_declared_dump_gets_an_upload_path(monkeypatch):
    # The device says how big the dump is; the broker answers with where to put
    # it. The path carries the report_id, which is the only capability the
    # device ever holds -- an unguessable UUID4, issued over TLS.
    import handler
    stored = {}
    monkeypatch.setattr(handler, '_clients',
                        lambda: (None, _FakeTable(stored)))
    res = handler.handler({'body': json.dumps(
        {'bt': 'riscv-no-unwind', 'raw_bytes': 65536})}, None)
    out = json.loads(res['body'])
    assert out['upload']['path'] == '/v1/reports/%s/raw' % out['report_id']
    # The device cannot parse a full symbolized trace and never reads one.
    assert 'frames' not in out
    assert stored['item']['raw'] == 'expected'
    assert stored['item']['raw_bytes'] == 65536


def test_no_dump_declared_means_no_upload_path(monkeypatch):
    # scripts/symbolize_crash.py posts a summary and no image. That report is
    # complete on arrival and must not sit forever waiting for bytes.
    import handler
    stored = {}
    monkeypatch.setattr(handler, '_clients',
                        lambda: (None, _FakeTable(stored)))
    res = handler.handler({'body': json.dumps({'bt': 'riscv-no-unwind'})}, None)
    assert 'upload' not in json.loads(res['body'])
    assert 'frames' in json.loads(res['body'])   # the CLI still gets the trace
    assert stored['item']['raw'] == 'none'
    assert stored['item']['complete'] is True


def test_an_absurd_declared_size_is_refused_without_reserving_anything(monkeypatch):
    # Review Focus 5. 10 MB is inside API Gateway's cap but nowhere near a
    # coredump partition; accepting the declaration would let a client park an
    # 'expected' row and then push 10 MB through the Lambda.
    import handler
    stored = {}
    monkeypatch.setattr(handler, '_clients',
                        lambda: (None, _FakeTable(stored)))
    for bad in (0, -1, 10 * 1024 * 1024, 'lots', None, 1.5):
        stored.clear()
        res = handler.handler(
            {'body': json.dumps({'bt': 'riscv-no-unwind', 'raw_bytes': bad})},
            None)
        assert 'upload' not in json.loads(res['body']), bad
        assert stored['item']['raw'] == 'none', bad
```

and, at the top of the same file, `import json` (the file imports only two
names from `handler` today, so the new tests will otherwise fail with
`NameError: json` rather than the intended assertion) plus the fake table they
share:

```python
import json


class _FakeTable:
    """Captures the single put_item the handler makes."""
    def __init__(self, sink):
        self._sink = sink

    def put_item(self, Item):
        self._sink['item'] = Item
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest test_handler.py -v
```

Expected: FAIL — `KeyError: 'upload'` on the first, `KeyError: 'raw'` on the second.

- [ ] **Step 3: Implement**

In `lambda/symbolize/handler.py`, below `MAX_FRAMES`:

```python
# The coredump partition is 0x10000 in every layout this firmware ships
# (min_spiffs.csv, min_spiffs_debug.csv, openevse_16mb.csv). The ceiling is
# generous against a future layout; the point is to refuse a client that wants
# to push megabytes through a Lambda, not to pin today's partition size.
MAX_RAW_BYTES = 128 * 1024

# How long an 'expected' row will accept its bytes. The device sends the dump
# in the same burst as the metadata; anything much later is a retry against a
# report that has already been counted, or a replay.
RAW_WINDOW_SECONDS = 900


def declared_raw_bytes(v):
    """The byte count the device says it will send, or None.

    bool is excluded explicitly: `isinstance(True, int)` is True in Python, and
    a JSON `true` here would otherwise reserve a 1-byte upload.
    """
    if isinstance(v, bool) or not isinstance(v, int):
        return None
    if v < 1 or v > MAX_RAW_BYTES:
        return None
    return v
```

Then, in `handler()`, replace the `table.put_item(...)` call and the return with:

```python
    now = datetime.now(timezone.utc)
    raw_bytes = declared_raw_bytes(body.get('raw_bytes'))

    _, table = _clients()
    table.put_item(Item={
        'report_id': report_id,
        'elf_sha256': _key_str(sha),
        'version': body.get('version', 'unknown'),
        'buildenv': body.get('buildenv', 'unknown'),
        'chip_id': _key_str(body.get('chip_id')),
        'created_at': now.isoformat(),
        'status': status,
        'frames': frames,
        'summary': body.get('summary', {}),
        'diagnostics': body.get('diagnostics', {}),
        # A report with no image to wait for is whole the moment it is written.
        # Without this, every CLI-posted summary would look permanently
        # unfinished in the index.
        'raw': 'expected' if raw_bytes else 'none',
        'raw_bytes': raw_bytes or 0,
        'complete': raw_bytes is None,
        'expires_at': int((now + timedelta(days=RETENTION_DAYS)).timestamp()),
    })

    out = {'report_id': report_id, 'status': status}
    if raw_bytes:
        # No frames for a device: a 16-frame symbolized trace carries full
        # build-host paths and runs to 2-4 KB of JSON, which will not fit the
        # parser on an ESP32 that is about to spend its heap on a TLS session.
        # The device only ever reads report_id. The CLI, which declares no
        # dump, still gets the trace it came for.
        out['upload'] = {'path': '/v1/reports/%s/raw' % report_id,
                         'expires_in': RAW_WINDOW_SECONDS}
    else:
        out['frames'] = frames

    return {
        'statusCode': 200,
        'headers': {'content-type': 'application/json'},
        'body': json.dumps(out),
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest -v
```

Expected: PASS, all tests in the directory (19 from Plan A plus the 3 new).

- [ ] **Step 5: Commit**

```bash
cd /home/rar/oevse/openevse-crash-service
git add lambda/symbolize/handler.py lambda/symbolize/test_handler.py
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: metadata POST issues an upload ticket for a declared dump"
```

---

### Task 2: The raw dump PUT

**Files:**
- Create: `lambda/symbolize/raw.py`
- Create: `lambda/symbolize/test_raw.py`
- Modify: `lambda/symbolize/handler.py` (route dispatch)

**Interfaces:**
- Consumes: Task 1's `raw`, `raw_bytes`, `created_at` item fields and `MAX_RAW_BYTES`.
- Produces: `handle_raw(report_id, event, now, s3, table, bucket) -> (int, dict)`; objects at `reports/<report_id>/coredump.bin`; items move `raw: 'expected' -> 'stored'`. Task 3 reads that transition.

This is an unauthenticated write endpoint. The `report_id` is the capability, so every other property has to be checked: that the row exists, that it is still expecting bytes, that it has not gone stale, and that the body is exactly the size that was declared.

- [ ] **Step 1: Write the failing tests**

Create `lambda/symbolize/test_raw.py`:

```python
import base64
from datetime import datetime, timedelta, timezone

import pytest

import raw

NOW = datetime(2026, 9, 28, 12, 0, 0, tzinfo=timezone.utc)
DUMP = b'\x00\xff' * 32768          # 64 KB, full of NULs on purpose


class FakeTable:
    def __init__(self, item):
        self.item = item
        self.updated = None

    def get_item(self, **kw):
        return {'Item': self.item} if self.item else {}

    def update_item(self, **kw):
        if self.item.get('raw') != 'expected':
            raise raw.ConditionFailed()
        self.item['raw'] = 'stored'
        self.updated = kw


class FakeS3:
    def __init__(self):
        self.put = None

    def put_object(self, **kw):
        self.put = kw


def live_item(**over):
    item = {'report_id': 'r1', 'raw': 'expected', 'raw_bytes': len(DUMP),
            'created_at': (NOW - timedelta(seconds=5)).isoformat()}
    item.update(over)
    return item


def event(body=DUMP, b64=True):
    return {'body': base64.b64encode(body).decode() if b64 else body,
            'isBase64Encoded': b64}


def test_a_declared_dump_is_stored_and_the_row_moves_to_stored():
    t, s3 = FakeTable(live_item()), FakeS3()
    code, out = raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')
    assert code == 200 and out['stored'] == len(DUMP)
    assert s3.put['Key'] == 'reports/r1/coredump.bin'
    assert s3.put['Body'] == DUMP          # NULs survive; base64, not strlen
    assert t.item['raw'] == 'stored'


def test_an_unknown_report_id_is_404_and_stores_nothing():
    # Review Focus 4. Without this, anyone can create objects in the bucket by
    # PUTting to invented ids.
    t, s3 = FakeTable(None), FakeS3()
    code, _ = raw.handle_raw('nope', event(), NOW, s3, t, 'bkt')
    assert code == 404 and s3.put is None


def test_a_report_that_already_has_its_dump_is_409():
    # Review Focus 4. A retry after a successful PUT must not overwrite; the
    # second image would be as authentic-looking as the first.
    t, s3 = FakeTable(live_item(raw='stored')), FakeS3()
    code, _ = raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')
    assert code == 409 and s3.put is None


def test_a_report_that_declared_no_dump_is_409():
    t, s3 = FakeTable(live_item(raw='none', raw_bytes=0)), FakeS3()
    code, _ = raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')
    assert code == 409 and s3.put is None


def test_a_stale_ticket_is_410_and_stores_nothing():
    # Review Focus 4. An 'expected' row is a standing write permit; it expires.
    old = (NOW - timedelta(seconds=raw.RAW_WINDOW_SECONDS + 1)).isoformat()
    t, s3 = FakeTable(live_item(created_at=old)), FakeS3()
    code, _ = raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')
    assert code == 410 and s3.put is None


def test_a_body_that_is_not_the_declared_size_is_400():
    # Review Focus 5. Accepting a short body would store a truncated memory
    # image that reads as complete.
    t, s3 = FakeTable(live_item()), FakeS3()
    code, _ = raw.handle_raw('r1', event(DUMP[:100]), NOW, s3, t, 'bkt')
    assert code == 400 and s3.put is None


def test_an_empty_body_is_400():
    t, s3 = FakeTable(live_item()), FakeS3()
    code, _ = raw.handle_raw('r1', event(b''), NOW, s3, t, 'bkt')
    assert code == 400 and s3.put is None


def test_undecodable_base64_is_400_not_a_500():
    t, s3 = FakeTable(live_item()), FakeS3()
    code, _ = raw.handle_raw('r1', {'body': '!!!!', 'isBase64Encoded': True},
                             NOW, s3, t, 'bkt')
    assert code == 400 and s3.put is None


def test_a_concurrent_duplicate_put_loses_the_race_at_the_condition():
    # Both callers read 'expected' -- the pre-check cannot separate them, so
    # this drives the case it misses: get_item still says expected while
    # update_item has already been claimed. The loser must not write bytes.
    class RacedTable(FakeTable):
        def update_item(self, **kw):
            raise raw.ConditionFailed()

    t, s3 = RacedTable(live_item()), FakeS3()
    code, _ = raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')
    assert code == 409
    assert s3.put is None       # the loser must not overwrite the winner


def test_a_sequential_retry_is_refused_at_the_pre_check():
    item = live_item()
    t, s3 = FakeTable(item), FakeS3()
    assert raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')[0] == 200
    code, _ = raw.handle_raw('r1', event(), NOW, s3, t, 'bkt')
    assert code == 409
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest test_raw.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'raw'`.

- [ ] **Step 3: Implement**

Create `lambda/symbolize/raw.py`:

```python
"""The raw core-dump PUT.

Unauthenticated by design: a charger holds no credential (spec §3). The
report_id is the capability -- a UUID4 minted by the broker and handed back
over TLS -- so every other property of the request is checked here rather than
assumed.

The dump does NOT go to a presigned S3 URL. Spec §4 requires the device to
allowlist every host it sends a memory image to, and a presigned URL lives on
*.s3.<region>.amazonaws.com; allowlisting that would permit a compromised
broker to redirect the image to any bucket on the internet, which is the exact
case the allowlist exists to prevent. 64 KB through a Lambda is cheap.
"""

import base64
from datetime import datetime, timedelta

MAX_RAW_BYTES = 128 * 1024
RAW_WINDOW_SECONDS = 900


class ConditionFailed(Exception):
    """Raised by the table when the conditional update loses its race.

    Defined here rather than caught as botocore's ConditionalCheckFailed so the
    tests can drive the same path without botocore installed; the handler maps
    the real exception onto it.
    """


def decode_body(event):
    """The request body as bytes, or None if it cannot be read.

    API Gateway base64-encodes a binary body and sets isBase64Encoded. A core
    dump is full of NULs and high bytes, so this is the only path that survives
    it intact -- the text path exists for tests and for a client that sends a
    declared-text body, which will fail the length check anyway.
    """
    body = event.get('body')
    if body is None:
        return b''
    if event.get('isBase64Encoded'):
        try:
            return base64.b64decode(body, validate=True)
        except Exception:
            return None
    if isinstance(body, bytes):
        return body
    return body.encode('utf-8', 'surrogateescape')


def handle_raw(report_id, event, now, s3, table, bucket):
    """(status_code, body_dict). Never raises for bad input."""
    data = decode_body(event)
    if data is None:
        return 400, {'msg': 'undecodable body'}

    # ConsistentRead: the device PUTs within one round trip of the metadata
    # POST that wrote this row, and an eventually-consistent GetItem can miss a
    # write that recent. The 404 that produces is intermittent, looks exactly
    # like a firmware bug, and strands the dump.
    item = table.get_item(Key={'report_id': report_id},
                          ConsistentRead=True).get('Item')
    if not item:
        return 404, {'msg': 'no such report'}

    if item.get('raw') != 'expected':
        # Either this report never declared a dump, or it already has one.
        # Overwriting is worse than refusing: a second image would look every
        # bit as authentic as the first.
        return 409, {'msg': 'not expecting a dump'}

    try:
        created = datetime.fromisoformat(item['created_at'])
    except (KeyError, TypeError, ValueError):
        return 410, {'msg': 'unreadable ticket'}
    if now - created > timedelta(seconds=RAW_WINDOW_SECONDS):
        return 410, {'msg': 'ticket expired'}

    declared = item.get('raw_bytes') or 0
    if len(data) != int(declared) or not 1 <= len(data) <= MAX_RAW_BYTES:
        # A short body stored as-is is a truncated memory image that reads as
        # a whole one.
        return 400, {'msg': 'length mismatch'}

    try:
        # Claim the row BEFORE writing the bytes. The read above and this write
        # are not atomic together, so the condition is what actually serialises
        # two simultaneous PUTs -- and doing it first means the loser of that
        # race never reaches put_object and so cannot overwrite the winner's
        # image with its own.
        #
        # The cost is the opposite failure: if put_object then fails, the row
        # says 'stored' with no object behind it. That is the better half of
        # the trade -- it is visible (the completion POST's caller gets a 503
        # and the report stays incomplete), where a silently overwritten memory
        # image is not.
        table.update_item(
            Key={'report_id': report_id},
            UpdateExpression='SET #r = :stored',
            ConditionExpression='#r = :expected',
            ExpressionAttributeNames={'#r': 'raw'},
            ExpressionAttributeValues={':stored': 'stored',
                                       ':expected': 'expected'})
    except ConditionFailed:
        return 409, {'msg': 'not expecting a dump'}

    s3.put_object(Bucket=bucket, Key='reports/%s/coredump.bin' % report_id,
                  Body=data, ContentType='application/octet-stream')

    return 200, {'stored': len(data)}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest test_raw.py -v
```

Expected: PASS, 10 tests.

- [ ] **Step 5: Route it, in `handler.py`**

Rename the existing entry point to `handle_metadata(event)` (everything from `try: body = json.loads(...)` to its `return`), then add above it:

```python
def _json(code, obj):
    return {'statusCode': code,
            'headers': {'content-type': 'application/json'},
            'body': json.dumps(obj)}


def handler(event, context):
    """Route on method and path.

    One Lambda for three routes: they share the table, the bucket client and a
    cold start, and the whole surface is three handlers.
    """
    method = (event.get('requestContext', {})
                   .get('http', {}).get('method', 'POST'))
    params = event.get('pathParameters') or {}
    report_id = params.get('report_id')

    if method == 'PUT' and report_id:
        import raw as raw_mod
        s3, table = _clients()
        try:
            code, out = raw_mod.handle_raw(
                report_id, event, datetime.now(timezone.utc), s3,
                _ConditionMapper(table), os.environ['BUCKET'])
        except Exception:
            # Losing a crash report is worse than an unsymbolized one, and it
            # is worse still to answer 500 to a device that will then keep the
            # dump and retry forever.
            return _json(503, {'msg': 'store unavailable'})
        return _json(code, out)

    return handle_metadata(event)
```

and the adapter that turns botocore's exception into the one `raw.py` declares:

```python
class _ConditionMapper:
    """Passes the table through, translating one botocore exception.

    raw.py must be importable and testable without botocore, so it declares its
    own ConditionFailed; this is the only place the real one is named.
    """
    def __init__(self, table):
        self._t = table

    def get_item(self, **kw):
        return self._t.get_item(**kw)

    def update_item(self, **kw):
        import botocore.exceptions
        import raw as raw_mod
        try:
            return self._t.update_item(**kw)
        except botocore.exceptions.ClientError as e:
            if (e.response.get('Error', {}).get('Code')
                    == 'ConditionalCheckFailedException'):
                raise raw_mod.ConditionFailed()
            raise
```

- [ ] **Step 6: Run the whole suite**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest -v
```

Expected: PASS, 32 tests.

- [ ] **Step 7: Commit**

```bash
cd /home/rar/oevse/openevse-crash-service
git add lambda/symbolize/raw.py lambda/symbolize/test_raw.py lambda/symbolize/handler.py
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: accept the raw core dump on the broker's own domain"
```

---

### Task 3: The completion POST

**Files:**
- Create: `lambda/symbolize/complete.py`
- Create: `lambda/symbolize/test_complete.py`
- Modify: `lambda/symbolize/handler.py` (one more route)

**Interfaces:**
- Consumes: Task 2's `raw: 'stored'` transition.
- Produces: `handle_complete(report_id, table) -> (int, dict)`; items gain `complete: True`. The device reads the 2xx as its cue to erase (spec §7).

It does not run `esp-coredump` (deviation D2). Its whole job is to be the signal that makes the device's erase safe — and to be a place a symbolization trigger can be added later without changing anything on the device.

- [ ] **Step 1: Write the failing tests**

Create `lambda/symbolize/test_complete.py`:

```python
import complete


class FakeTable:
    def __init__(self, item):
        self.item = item

    def get_item(self, **kw):
        return {'Item': self.item} if self.item else {}

    def update_item(self, **kw):
        self.item['complete'] = True


def test_a_stored_dump_completes():
    t = FakeTable({'report_id': 'r1', 'raw': 'stored', 'status': 'symbolized',
                   'frames': [{'func': 'loop'}]})
    code, out = complete.handle_complete('r1', t)
    assert code == 200
    assert t.item['complete'] is True
    assert out['status'] == 'symbolized' and out['frames'][0]['func'] == 'loop'


def test_completing_a_report_whose_dump_never_arrived_is_409():
    # Spec §7: the device erases only on a 2xx here. Answering 200 while the
    # image is missing would destroy an unrecoverable dump on the device.
    t = FakeTable({'report_id': 'r1', 'raw': 'expected'})
    code, _ = complete.handle_complete('r1', t)
    assert code == 409
    assert 'complete' not in t.item


def test_an_unknown_report_is_404():
    code, _ = complete.handle_complete('nope', FakeTable(None))
    assert code == 404


def test_completing_twice_is_idempotent():
    # The device retries a completion whose reply it never saw. A second call
    # must not turn a successful upload into a failure and strand the dump.
    t = FakeTable({'report_id': 'r1', 'raw': 'stored', 'complete': True,
                   'status': 'unsymbolized', 'frames': []})
    assert complete.handle_complete('r1', t)[0] == 200
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest test_complete.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'complete'`.

- [ ] **Step 3: Implement**

Create `lambda/symbolize/complete.py`:

```python
"""Mark a report whole.

Deliberately not where symbolization happens (deviation D2): the addr2line
trace was produced when the metadata arrived, and esp-coredump against the raw
image is separate work. What this route exists for is spec §7 -- the device
erases its only copy of the dump on a 2xx from here, so this must answer 200
only when the bytes are actually stored.
"""


def handle_complete(report_id, table):
    """(status_code, body_dict)."""
    # Consistent for the same reason as the raw PUT: this arrives one round
    # trip after the write it is reading.
    item = table.get_item(Key={'report_id': report_id},
                          ConsistentRead=True).get('Item')
    if not item:
        return 404, {'msg': 'no such report'}

    if item.get('raw') == 'expected':
        # The dump was promised and never arrived. Saying 200 here would tell
        # the device to erase an image nobody has.
        return 409, {'msg': 'dump not stored'}

    if not item.get('complete'):
        table.update_item(
            Key={'report_id': report_id},
            UpdateExpression='SET complete = :t',
            ExpressionAttributeValues={':t': True})

    return 200, {'report_id': report_id,
                 'status': item.get('status', 'unknown'),
                 'raw': item.get('raw', 'none'),
                 'frames': item.get('frames', [])}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest test_complete.py -v
```

Expected: PASS, 4 tests.

- [ ] **Step 5: Route it**

In `handler.py`, inside `handler()`, immediately before the final `return handle_metadata(event)`:

```python
    if method == 'POST' and report_id:
        import complete as complete_mod
        _, table = _clients()
        try:
            code, out = complete_mod.handle_complete(report_id, table)
        except Exception:
            return _json(503, {'msg': 'store unavailable'})
        return _json(code, out)
```

- [ ] **Step 6: Run the whole suite**

```bash
cd /home/rar/oevse/openevse-crash-service/lambda/symbolize && python3 -m pytest -v
```

Expected: PASS, 36 tests.

- [ ] **Step 7: Commit**

```bash
cd /home/rar/oevse/openevse-crash-service
git add lambda/symbolize/complete.py lambda/symbolize/test_complete.py lambda/symbolize/handler.py
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: completion route, so the device knows when erasing is safe"
```

---

### Task 4: Wire the routes and the permissions into the stack

**Files:**
- Modify: `lib/symbolize-stack.ts`
- Modify: `test/symbolize-stack.test.ts`

**Interfaces:**
- Consumes: the `fn`, `api`, `props.bucket` and `props.table` already in `SymbolizeStack` (Plan A).
- Produces: routes `PUT /v1/reports/{report_id}/raw` and `POST /v1/reports/{report_id}/complete`; the Lambda gains `s3:PutObject` on `reports/*` and read+update on the table.

Plan A's Lambda has `grantWriteData` on the table and `grantRead(fn, 'elf/*')` on the bucket — it can write index rows and read ELFs, and nothing else. Both new routes need more: reading a row back, updating it, and writing an object under `reports/`. The ELF prefix stays read-only, so a bug in the upload path still cannot damage the archive that makes every other report readable.

- [ ] **Step 1: Write the failing tests**

Append to `test/symbolize-stack.test.ts`:

```typescript
test('both upload routes exist on the versioned path', () => {
  const routes = Object.values(template().findResources('AWS::ApiGatewayV2::Route'))
    .map((r: any) => r.Properties.RouteKey);
  expect(routes).toContain('PUT /v1/reports/{report_id}/raw');
  expect(routes).toContain('POST /v1/reports/{report_id}/complete');
  expect(routes).toContain('POST /v1/reports');
});

test('the lambda can write reports but still cannot write ELFs', () => {
  // The ELF archive is what makes every report readable. A bug in the upload
  // path must not be able to overwrite it.
  //
  // Checked statement by statement rather than by substring: both prefixes and
  // both actions appear somewhere in the rendered JSON either way, so a
  // toContain/not.toContain pair here would pass without ever proving they are
  // not in the SAME statement -- a test that asserts nothing.
  const statements = Object.values(template().findResources('AWS::IAM::Policy'))
    .flatMap((r: any) => r.Properties.PolicyDocument.Statement);
  const writesToElf = statements.some((st: any) => {
    const actions = [st.Action].flat();
    const resources = JSON.stringify(st.Resource ?? '');
    return actions.some((a: string) => typeof a === 'string' &&
             (a === 's3:*' || a.startsWith('s3:Put') || a.startsWith('s3:Delete')))
           && resources.includes('elf/');
  });
  expect(writesToElf).toBe(false);

  const writesToReports = statements.some((st: any) =>
    [st.Action].flat().includes('s3:PutObject') &&
    JSON.stringify(st.Resource ?? '').includes('reports/'));
  expect(writesToReports).toBe(true);
});

test('the lambda can read a report row back', () => {
  // handle_raw and handle_complete both GetItem before deciding. Without this
  // every PUT AccessDenies and the device retries a dump it can never store.
  expect(JSON.stringify(template().findResources('AWS::IAM::Policy')))
    .toContain('dynamodb:GetItem');
});

test('the raw-dump route is throttled harder than the index', () => {
  // 128 KB a call at the stage's default 10 rps is ~110 GB/day into a bucket
  // that keeps it for 90 days, and the endpoint is unauthenticated by design.
  // The stage-wide throttle does not bound that; this does.
  template().hasResourceProperties('AWS::ApiGatewayV2::Stage', {
    RouteSettings: {
      'PUT /v1/reports/{report_id}/raw': {
        ThrottlingBurstLimit: 5,
        ThrottlingRateLimit: 1,
      },
    },
  });
});
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cd /home/rar/oevse/openevse-crash-service && npx jest test/symbolize-stack.test.ts
```

Expected: FAIL — the route list has only `POST /v1/reports`, and the policy has no `dynamodb:GetItem`.

- [ ] **Step 3: Implement**

In `lib/symbolize-stack.ts`, replace `props.table.grantWriteData(fn);` with:

```typescript
    // Read as well as write: handle_raw and handle_complete both GetItem the
    // row before deciding whether the request is allowed at all.
    props.table.grantReadWriteData(fn);
    // Reports only. elf/* stays read-only for this function -- the archive is
    // what makes every other report readable, and nothing in the upload path
    // has any reason to touch it.
    props.bucket.grantPut(fn, 'reports/*');
```

and replace the single `api.addRoutes({...})` call with:

```typescript
    const integration = new integrations.HttpLambdaIntegration('Symbolize', fn);

    api.addRoutes({
      path: '/v1/reports',
      methods: [apigwv2.HttpMethod.POST],
      integration,
    });
    // The raw memory image, on the broker's own domain rather than a presigned
    // S3 URL: the firmware allowlists every host it sends one to (spec §4), and
    // allowlisting S3 would let a compromised broker redirect it anywhere.
    api.addRoutes({
      path: '/v1/reports/{report_id}/raw',
      methods: [apigwv2.HttpMethod.PUT],
      integration,
    });
    api.addRoutes({
      path: '/v1/reports/{report_id}/complete',
      methods: [apigwv2.HttpMethod.POST],
      integration,
    });

    // The raw route carries up to 128 KB a call, so the stage's own 10 rps
    // would admit ~110 GB a day into a bucket that keeps it for 90 days. A
    // charger uploads one dump, rarely; one a second across the whole fleet is
    // already generous. `stage` is the CfnStage the default throttle is set on
    // a few lines above.
    stage.routeSettings = {
      'PUT /v1/reports/{report_id}/raw': {
        throttlingBurstLimit: 5,
        throttlingRateLimit: 1,
      },
    };
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /home/rar/oevse/openevse-crash-service && npx jest
```

Expected: PASS, 21 tests.

- [ ] **Step 5: Deploy and exercise it end to end**

```bash
cd /home/rar/oevse/openevse-crash-service
npx cdk deploy --all -c region=us-east-2 --require-approval never
```

Then, against the deployed API (substitute the `ApiUrl` output):

```bash
API=https://ci84ymdemf.execute-api.us-east-2.amazonaws.com
head -c 65536 /dev/urandom > /tmp/dump.bin
ID=$(curl -s -X POST "$API/v1/reports" -H 'content-type: application/json' \
  -d '{"bt":"riscv-no-unwind","version":"plan-b-smoke","raw_bytes":65536}' \
  | tee /dev/stderr | python3 -c 'import sys,json; print(json.load(sys.stdin)["report_id"])')
curl -s -o /dev/null -w '%{http_code}\n' -X PUT "$API/v1/reports/$ID/raw" \
  -H 'content-type: application/octet-stream' --data-binary @/tmp/dump.bin
curl -s -X POST "$API/v1/reports/$ID/complete"
curl -s -o /dev/null -w 'replay=%{http_code}\n' -X PUT "$API/v1/reports/$ID/raw" \
  -H 'content-type: application/octet-stream' --data-binary @/tmp/dump.bin
```

Expected: the PUT prints `200`, the completion prints a JSON body with `"raw": "stored"`, and the replay prints `replay=409`. Confirm the object landed and its size is exactly 65536:

```bash
aws s3api head-object --region us-east-2 \
  --bucket crashstorage-crashbucketed041c25-mulnvmsdrfn4 \
  --key "reports/$ID/coredump.bin" --query ContentLength
```

Expected: `65536`.

- [ ] **Step 6: Commit**

```bash
cd /home/rar/oevse/openevse-crash-service
git add lib/symbolize-stack.ts test/symbolize-stack.test.ts
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: upload routes and the permissions they need"
```

---

### Task 5: The stable custom domain

**Files:**
- Create: `lib/domain-stack.ts`
- Create: `test/domain-stack.test.ts`
- Modify: `lib/symbolize-stack.ts` (expose the API)
- Modify: `bin/openevse-crash-service.ts`

**Interfaces:**
- Consumes: `SymbolizeStack`, which gains `public readonly api: apigwv2.HttpApi`.
- Produces: `DomainStack`, instantiated only when configured. Nothing in the firmware depends on this having run — the firmware pins the hostname regardless (Task 7), so the stack and the fleet converge whenever DNS does.

Spec §4's whole point: the firmware pins a name, not an API Gateway hostname, so moving the stack to OpenEVSE's account is a DNS change rather than a fleet reflash. That means the domain has to be a first-class part of the stack, and it has to be **optional**, because `openevse.com` is not delegated to this account today. Three configurations, all real:

- **neither `hostedZoneId` nor `certificateArn`** — the stack is not created. `cdk deploy --all` still works and the API keeps its generated hostname. This is today.
- **`certificateArn` only** — someone validated a certificate by hand (added the ACM CNAME to a zone they control elsewhere). The stack creates the custom domain and the mapping; the operator points the CNAME at the regional target themselves.
- **`hostedZoneId` + `zoneName`** — the account controls the zone. The stack issues and validates the certificate and writes the alias record. This is the end state once `openevse.com` (or a delegated `crash.openevse.com`) is in the account.

- [ ] **Step 1: Write the failing tests**

Create `test/domain-stack.test.ts`:

```typescript
import * as cdk from 'aws-cdk-lib';
import { Template } from 'aws-cdk-lib/assertions';
import { StorageStack } from '../lib/storage-stack';
import { SymbolizeStack } from '../lib/symbolize-stack';
import { DomainStack } from '../lib/domain-stack';

const HOSTNAME = 'crash.openevse.com';
const CERT = 'arn:aws:acm:us-east-2:123456789012:certificate/abc';

function build(props: any) {
  const app = new cdk.App();
  const env = { account: '123456789012', region: 'us-east-2' };
  const storage = new StorageStack(app, 'S', { env });
  const sym = new SymbolizeStack(app, 'T', {
    env, bucket: storage.bucket, table: storage.table,
    githubOrg: 'OpenEVSE', githubRepo: 'openevse_esp32_firmware',
    oidcProviderArn: 'arn:aws:iam::123456789012:oidc-provider/x',
  });
  return Template.fromStack(new DomainStack(app, 'D', {
    env, api: sym.api, hostname: HOSTNAME, ...props,
  }));
}

test('an existing certificate is used, not reissued', () => {
  // Validating a certificate needs control of the zone. When someone has
  // already done that by hand, reissuing would block the deploy on a DNS
  // record this account cannot write.
  const t = build({ certificateArn: CERT });
  expect(t.findResources('AWS::CertificateManager::Certificate')).toEqual({});
  t.hasResourceProperties('AWS::ApiGatewayV2::DomainName', {
    DomainName: HOSTNAME,
  });
});

test('no alias record is written for a zone this account does not hold', () => {
  // Without a hosted zone there is nothing to write into, and attempting it
  // fails the deploy rather than degrading.
  expect(build({ certificateArn: CERT })
    .findResources('AWS::Route53::RecordSet')).toEqual({});
});

test('with a hosted zone the certificate and the alias are both created', () => {
  const t = build({ hostedZoneId: 'Z123', zoneName: 'openevse.com' });
  t.hasResourceProperties('AWS::CertificateManager::Certificate', {
    DomainName: HOSTNAME,
    ValidationMethod: 'DNS',
  });
  t.hasResourceProperties('AWS::Route53::RecordSet', { Type: 'A' });
});

test('the api mapping points at the deployed stage', () => {
  // A domain with no mapping resolves and then 404s every request, which looks
  // exactly like a firmware bug from the device end.
  build({ certificateArn: CERT })
    .resourceCountIs('AWS::ApiGatewayV2::ApiMapping', 1);
});
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cd /home/rar/oevse/openevse-crash-service && npx jest test/domain-stack.test.ts
```

Expected: FAIL — `Cannot find module '../lib/domain-stack'`.

- [ ] **Step 3: Implement**

In `lib/symbolize-stack.ts`, add the field and assign it where the API is built:

```typescript
export class SymbolizeStack extends cdk.Stack {
  public readonly api: apigwv2.HttpApi;
```

(change `const api = new apigwv2.HttpApi(...)` to `this.api = new apigwv2.HttpApi(...)` and use `this.api` in the lines that follow.)

Create `lib/domain-stack.ts`:

```typescript
import * as cdk from 'aws-cdk-lib';
import * as acm from 'aws-cdk-lib/aws-certificatemanager';
import * as apigwv2 from 'aws-cdk-lib/aws-apigatewayv2';
import * as route53 from 'aws-cdk-lib/aws-route53';
import * as targets from 'aws-cdk-lib/aws-route53-targets';
import { Construct } from 'constructs';

export interface DomainStackProps extends cdk.StackProps {
  api: apigwv2.HttpApi;
  /** The name the firmware pins. Compiled into every charger, so it changes
   *  only by reflashing a fleet -- treat it as immutable. */
  hostname: string;
  /** A certificate validated elsewhere. Takes precedence over issuing one. */
  certificateArn?: string;
  /** Set both when this account holds the zone; the stack then issues the
   *  certificate and writes the alias itself. */
  hostedZoneId?: string;
  zoneName?: string;
}

/** The stable hostname spec §4 requires.
 *
 *  The stack will move accounts -- RAR runs it, OpenEVSE takes it over. Every
 *  charger has the hostname compiled in, so that handover has to be a DNS
 *  change; if the firmware pinned the generated execute-api hostname instead,
 *  it would be a fleet reflash. */
export class DomainStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props: DomainStackProps) {
    super(scope, id, props);

    const zone = props.hostedZoneId && props.zoneName
      ? route53.HostedZone.fromHostedZoneAttributes(this, 'Zone', {
          hostedZoneId: props.hostedZoneId,
          zoneName: props.zoneName,
        })
      : undefined;

    // An HTTP API custom domain is regional, so the certificate lives in the
    // API's own region -- not us-east-1, which is the CloudFront rule and the
    // usual way this is got wrong.
    const certificate: acm.ICertificate = props.certificateArn
      ? acm.Certificate.fromCertificateArn(this, 'Cert', props.certificateArn)
      : new acm.Certificate(this, 'Cert', {
          domainName: props.hostname,
          validation: acm.CertificateValidation.fromDns(zone),
        });

    const domain = new apigwv2.DomainName(this, 'Domain', {
      domainName: props.hostname,
      certificate,
    });

    // Without a mapping the domain resolves and 404s everything, which from a
    // charger is indistinguishable from a firmware bug.
    new apigwv2.ApiMapping(this, 'Mapping', {
      api: props.api,
      domainName: domain,
      // defaultStage is typed optional; HttpApi always creates one unless
      // createDefaultStage was disabled, which it is not here.
      stage: props.api.defaultStage!,
    });

    if (zone) {
      new route53.ARecord(this, 'Alias', {
        zone,
        recordName: props.hostname,
        target: route53.RecordTarget.fromAlias(
          new targets.ApiGatewayv2DomainProperties(
            domain.regionalDomainName, domain.regionalHostedZoneId)),
      });
    }

    new cdk.CfnOutput(this, 'Hostname', { value: props.hostname });
    // What to CNAME by hand when this account does not hold the zone.
    new cdk.CfnOutput(this, 'RegionalTarget',
      { value: domain.regionalDomainName });
  }
}
```

In `bin/openevse-crash-service.ts`, after the `SymbolizeStack` construction:

```typescript
// The custom domain is optional and absent by default: openevse.com is not
// delegated to this account, so `cdk deploy --all` has to keep working without
// it. The firmware pins the hostname regardless (spec §4), so the fleet and the
// stack converge whenever DNS does -- no firmware change is waiting on this.
//
//   npx cdk deploy --all -c hostname=crash.openevse.com \
//     -c hostedZoneId=Z... -c zoneName=openevse.com
//   npx cdk deploy --all -c hostname=crash.openevse.com -c certificateArn=arn:...
const hostname = app.node.tryGetContext('hostname');
const certificateArn = app.node.tryGetContext('certificateArn');
const hostedZoneId = app.node.tryGetContext('hostedZoneId');
// hostedZoneId without zoneName would reach CertificateValidation.fromDns
// (undefined), which falls back to manual validation and hangs the deploy for
// half an hour before failing. Require the pair.
const zoneName = app.node.tryGetContext('zoneName');
if (hostname && (certificateArn || (hostedZoneId && zoneName))) {
  new DomainStack(app, 'CrashDomain', {
    env,
    api: symbolize.api,
    hostname,
    certificateArn,
    hostedZoneId,
    zoneName,
  });
}
```

(the `SymbolizeStack` construction needs `const symbolize =` in front of it, and `DomainStack` importing.)

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /home/rar/oevse/openevse-crash-service && npx jest
```

Expected: PASS, 25 tests.

- [ ] **Step 5: Verify the default deploy is unchanged**

```bash
cd /home/rar/oevse/openevse-crash-service && npx cdk list -c region=us-east-2
```

Expected: `CrashStorage` and `CrashSymbolize` only — **no** `CrashDomain`. The domain is opt-in; a synth that produces it by accident would try to validate a certificate this account cannot validate and hang the deploy for 30 minutes before failing.

- [ ] **Step 6: Commit**

```bash
cd /home/rar/oevse/openevse-crash-service
git add lib/domain-stack.ts lib/symbolize-stack.ts bin/openevse-crash-service.ts test/domain-stack.test.ts
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: optional custom domain, so handover is a DNS change"
```

---

### Task 6: The two pure pieces — host allowlist and config redaction

**Repo:** `/home/rar/oevse/openevse_esp32_firmware`, branch `feat/crash-upload` off **`feat/crash-elf-archive`** (rebase that onto `oe-ssh/master` first), in a worktree.

Not off master: `scripts/symbolize_crash.py` and the CI ELF-archive step exist only on `feat/crash-elf-archive` (Plan A, unpushed). Task 7 edits the first and Task 9's end-to-end run needs the second, so branching off master makes both unreachable.

**Files:**
- Create: `src/crash_host.h`
- Modify: `src/ota_url_allow.h`, `src/ota_url_allow.cpp`
- Create: `src/crash_redact.h`, `src/crash_redact.cpp`
- Create: `test/test_crash_url_allow/test_crash_url_allow.cpp`
- Create: `test/test_crash_redact/test_crash_redact.cpp`
- Modify: `platformio.ini` (`native_test` `build_src_filter`)

**Interfaces:**
- Produces: `CRASH_BROKER_HOST`; `bool crash_url_host_allowed(const char *url)`; `bool crash_redact_key_allowed(const char *key)`; `void crash_redact_config(JsonObjectConst full, JsonObject out)`. Tasks 7 and 8 consume all four.

Both of these are the security surface of the whole feature, and both are pure functions of their inputs — so they get built first, on the host, where they can be tested exhaustively without a board. The allowlist is deliberately stricter than the OTA one: firmware comes from a CDN with several legitimate hostnames, whereas a memory image has exactly one destination, ever.

- [ ] **Step 1: Write the failing tests**

Create `test/test_crash_url_allow/test_crash_url_allow.cpp`:

```cpp
// Host-side tests for the crash-broker URL allowlist (ota_url_allow.cpp).
// A core dump is a RAM image carrying the WiFi PSK and every stored token, so
// this list is exact-match and single-entry -- unlike the OTA list next to it,
// which has to admit a CDN.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ota_url_allow.h"
#include "crash_host.h"

TEST_CASE("the compiled-in broker host is allowed") {
  CHECK(crash_url_host_allowed("https://" CRASH_BROKER_HOST "/v1/reports"));
  CHECK(crash_url_host_allowed("HTTPS://" CRASH_BROKER_HOST "/v1/reports"));
  CHECK(crash_url_host_allowed("https://" CRASH_BROKER_HOST ":443/v1/reports"));
}

TEST_CASE("nothing else is, including neighbours of the real host") {
  // Subdomains are NOT admitted: the OTA list accepts *.githubusercontent.com
  // because a CDN needs it, and copying that here would mean anyone who can
  // create a record under the zone can receive memory images.
  CHECK_FALSE(crash_url_host_allowed("https://evil.crash.openevse.com/v1/reports"));
  CHECK_FALSE(crash_url_host_allowed("https://openevse.com/v1/reports"));
  CHECK_FALSE(crash_url_host_allowed("https://crash.openevse.com.evil.example/v1"));
  CHECK_FALSE(crash_url_host_allowed("https://github.com/v1/reports"));
  // S3 is explicitly not allowed -- this is why the raw PUT goes to the broker
  // rather than to a presigned URL (deviation D1).
  CHECK_FALSE(crash_url_host_allowed("https://bkt.s3.us-east-2.amazonaws.com/x?X-Amz-Signature=y"));
}

TEST_CASE("plain http is refused") {
  CHECK_FALSE(crash_url_host_allowed("http://" CRASH_BROKER_HOST "/v1/reports"));
  CHECK_FALSE(crash_url_host_allowed(CRASH_BROKER_HOST "/v1/reports"));
}

TEST_CASE("the userinfo bypass is refused here too") {
  CHECK_FALSE(crash_url_host_allowed("https://" CRASH_BROKER_HOST ":1@evil.example/v1"));
  CHECK_FALSE(crash_url_host_allowed("https://" CRASH_BROKER_HOST "?@evil.example/v1"));
  CHECK_FALSE(crash_url_host_allowed("https://" CRASH_BROKER_HOST "\\@evil.example/v1"));
}

TEST_CASE("nothing at all is refused rather than crashing") {
  CHECK_FALSE(crash_url_host_allowed(NULL));
  CHECK_FALSE(crash_url_host_allowed(""));
}

TEST_CASE("the OTA list is unchanged by the refactor") {
  CHECK(ota_url_host_allowed("https://github.com/OpenEVSE/x/fw.bin"));
  CHECK_FALSE(ota_url_host_allowed("https://" CRASH_BROKER_HOST "/fw.bin"));
}
```

Create `test/test_crash_redact/test_crash_redact.cpp`:

```cpp
// Host-side tests for the crash-report config redaction (crash_redact.cpp).
// Spec §5: an allowlist of key names, never a blocklist -- a blocklist starts
// leaking the day someone adds a credential-bearing option and forgets it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <ArduinoJson.h>
#include <string>

#include "crash_redact.h"

TEST_CASE("every credential-bearing key in app_config is dropped") {
  // These follow from the unknown-key case below rather than adding coverage
  // to it -- the function says no to every string not in ALLOWED. They are a
  // regression list: if someone ever "helpfully" widens the allowlist, this is
  // the test that says which names must never be on it. (hideSecrets=true
  // already masks most of them in config_serialize, but ocpp_authkey is a
  // plain ConfigOptDefinition<String> and is NOT masked -- the allowlist is
  // the only thing keeping it out.)
  const char *secrets[] = {
    "ap_pass", "pass", "www_password", "www_username", "server_secret",
    "mqtt_pass", "mqtt_user", "emoncms_apikey", "ocpp_authkey",
    "tesla_access_token", "tesla_refresh_token",
  };
  for(const char *k : secrets) {
    CHECK_FALSE(crash_redact_key_allowed(k));
  }
}

TEST_CASE("an option nobody has written yet is dropped by default") {
  // The property that makes this an allowlist: the answer for an unknown key
  // is no. If this ever becomes yes, the next credential option leaks.
  CHECK_FALSE(crash_redact_key_allowed("future_cloud_api_token"));
  CHECK_FALSE(crash_redact_key_allowed(""));
  CHECK_FALSE(crash_redact_key_allowed(NULL));
}

TEST_CASE("the feature flags that explain a crash are kept") {
  CHECK(crash_redact_key_allowed("mqtt_enabled"));
  CHECK(crash_redact_key_allowed("ocpp_enabled"));
  CHECK(crash_redact_key_allowed("divert_enabled"));
  CHECK(crash_redact_key_allowed("rfid_enabled"));
  CHECK(crash_redact_key_allowed("tesla_enabled"));
  CHECK(crash_redact_key_allowed("sntp_enabled"));
  CHECK(crash_redact_key_allowed("emoncms_enabled"));
  CHECK(crash_redact_key_allowed("loadsharing_enabled"));
  CHECK(crash_redact_key_allowed("current_shaper_enabled"));
  CHECK(crash_redact_key_allowed("temp_throttle_enabled"));
  CHECK(crash_redact_key_allowed("buildenv"));
  CHECK(crash_redact_key_allowed("version"));
}

TEST_CASE("copying a whole config carries the flags and nothing else") {
  StaticJsonDocument<512> full;
  full["mqtt_enabled"] = true;
  full["mqtt_pass"] = "hunter2";
  full["mqtt_server"] = "mqtt.example";   // not a secret, but not on the list
  full["version"] = "4.2.0";

  StaticJsonDocument<512> out;
  JsonObject o = out.to<JsonObject>();
  crash_redact_config(full.as<JsonObjectConst>(), o);

  CHECK(o["mqtt_enabled"].as<bool>() == true);
  CHECK(o["version"].as<const char *>() == std::string("4.2.0"));
  CHECK_FALSE(o.containsKey("mqtt_pass"));
  CHECK_FALSE(o.containsKey("mqtt_server"));
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cd /home/rar/oevse/openevse_esp32_firmware && scripts/pio test -e native_test -f test_crash_url_allow -f test_crash_redact
```

Expected: FAIL at compile — `crash_host.h: No such file or directory`.

- [ ] **Step 3: Implement the host constant**

Create `src/crash_host.h`:

```cpp
#ifndef CRASH_HOST_H
#define CRASH_HOST_H

// The single host a core dump may ever be sent to (spec §4).
//
// Compiled in, never a config value. A core dump is a RAM image: the WiFi PSK,
// the MQTT password, OCPP and Tesla tokens and any private key material are all
// in it. If the destination were an ordinary config key, one XSS or one
// authenticated but hostile write would redirect every charger's memory image
// to an attacker -- which, given this project's disclosure history, is a
// requirement rather than a nicety.
//
// The -D override exists for bench work against a test broker. It is a build
// flag, not a runtime value, so a released image still has exactly one
// destination.
#ifndef CRASH_BROKER_HOST
#define CRASH_BROKER_HOST "crash.openevse.com"
#endif

#endif // CRASH_HOST_H
```

- [ ] **Step 4: Refactor the URL parser and add the second allowlist**

In `src/ota_url_allow.cpp`, replace the body of `ota_url_host_allowed` with a shared parser. Everything from `std::string url = url_c;` down to `host = to_lower(host);` moves verbatim into:

```cpp
// Parse `url` to the host the HTTP client will actually connect to, or return
// false. Shared by both allowlists so they can never disagree about what the
// host is -- which is the bug class the "github.com:x@evil.example" tests exist
// for.
static bool url_host(const char *url_c, std::string &host_out)
{
  if(!url_c) {
    return false;
  }
  std::string url = url_c;

  // Scheme must be https (case-insensitive). Require the "://" separator so a
  // bare host without a scheme is rejected.
  std::string::size_type sep = url.find("://");
  if(sep == std::string::npos) {
    return false;
  }
  if(to_lower(url.substr(0, sep)) != "https") {
    return false;
  }

  std::string::size_type start = sep + 3;

  // The authority ends at the first '/' ONLY. Mongoose's userinfo scan
  // (mg_parse_uri, P_USER_INFO) terminates on '@', '[' or '/', so ':', '?' and
  // '#' are NOT authority delimiters for the host it actually connects to.
  // Ending on '?' here (as an earlier version did) would let
  // "https://github.com?@evil.example/..." parse to host "github.com" while the
  // client connects to "evil.example".
  std::string::size_type end = url.find('/', start);
  if(end == std::string::npos) {
    end = url.size();
  }
  std::string authority = url.substr(start, end - start);

  // Reject anything that could make our host differ from the client's, rather
  // than trying to re-derive the exact parser.
  if(authority.find_first_of("@[]?#\\ \t") != std::string::npos) {
    return false;
  }

  // Strip the port.
  std::string host = authority.substr(0, authority.find(':'));

  if(host.empty()) {
    return false;
  }

  // Host must be a plain DNS name (letters, digits, '.', '-').
  for(char c : host) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-';
    if(!ok) {
      return false;
    }
  }

  host_out = to_lower(host);
  return true;
}
```

This is the existing body of `ota_url_host_allowed` verbatim, with the trailing
allowlist comparison removed and the host written to `host_out`. Nothing about
the parsing changes, which is why `test_ota_url_allow` must still pass
unmodified.

and the two allowlists become:

```cpp
bool ota_url_host_allowed(const char *url_c)
{
  std::string host;
  if(!url_host(url_c, host)) {
    return false;
  }
  // Allowlist. Edit here to permit other trusted firmware hosts.
  return host_matches(host, "github.com") ||
         host_matches(host, "githubusercontent.com");
}

bool crash_url_host_allowed(const char *url_c)
{
  std::string host;
  if(!url_host(url_c, host)) {
    return false;
  }
  // Exact match, one entry, no subdomains -- deliberately stricter than the
  // OTA list above. Firmware comes from a CDN that needs several hostnames; a
  // memory image has exactly one destination, and admitting "*.the-zone" would
  // hand it to anyone who can create a record there.
  return host == CRASH_BROKER_HOST;
}
```

Add to `src/ota_url_allow.h`, with `#include "crash_host.h"` at the top:

```cpp
// True if `url` is an https URL on the one compiled-in crash-broker host
// (CRASH_BROKER_HOST). Exact match, no subdomains: see crash_host.h for why
// this is not a config value and why the list has exactly one entry.
bool crash_url_host_allowed(const char *url);
```

- [ ] **Step 5: Implement the redaction**

Create `src/crash_redact.h`:

```cpp
#ifndef CRASH_REDACT_H
#define CRASH_REDACT_H

#include <ArduinoJson.h>

// Spec §5. The crash report carries enough configuration to explain a crash
// and nothing that could authenticate anybody.
//
// An ALLOWLIST, never a blocklist: a blocklist looks correct right up until
// someone adds a credential-bearing option and does not think about this file,
// and then it silently leaks. The default answer for an unrecognised key is no.
bool crash_redact_key_allowed(const char *key);

// Copy the allowed keys from `full` into `out`, leaving everything else behind.
void crash_redact_config(JsonObjectConst full, JsonObject out);

#endif // CRASH_REDACT_H
```

Create `src/crash_redact.cpp`:

```cpp
#include "crash_redact.h"

#include <string.h>

// What a maintainer needs to reproduce a crash: which subsystems were running,
// and what build it was. Nothing here is a secret, and nothing here is a free
// text field a user could have pasted a secret into.
static const char *ALLOWED[] = {
  "buildenv",
  "version",
  "current_shaper_enabled",
  "divert_enabled",
  "emoncms_enabled",
  "loadsharing_enabled",
  "mqtt_enabled",
  "ocpp_enabled",
  "rfid_enabled",
  "sntp_enabled",
  "temp_throttle_enabled",
  "tesla_enabled",
};

bool crash_redact_key_allowed(const char *key)
{
  if(!key || '\0' == key[0]) {
    return false;
  }
  for(size_t i = 0; i < sizeof(ALLOWED) / sizeof(ALLOWED[0]); i++) {
    if(0 == strcmp(key, ALLOWED[i])) {
      return true;
    }
  }
  return false;
}

void crash_redact_config(JsonObjectConst full, JsonObject out)
{
  for(JsonPairConst kv : full) {
    if(crash_redact_key_allowed(kv.key().c_str())) {
      out[kv.key()] = kv.value();
    }
  }
}
```

- [ ] **Step 6: Add both sources to the host test build**

In `platformio.ini`, in `[env:native_test]`, append to `build_src_filter`:

```
+<crash_redact.cpp>
```

(`ota_url_allow.cpp` is already listed.)

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cd /home/rar/oevse/openevse_esp32_firmware && scripts/pio test -e native_test
```

Expected: PASS — every suite, including the pre-existing `test_ota_url_allow` (the refactor must not change its behaviour) and the two new ones.

- [ ] **Step 8: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/crash_host.h src/crash_redact.h src/crash_redact.cpp \
        src/ota_url_allow.h src/ota_url_allow.cpp platformio.ini \
        test/test_crash_url_allow test/test_crash_redact
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: pin the crash broker host and redact config by allowlist"
```

---

### Task 7: The uploader

**Files:**
- Create: `src/crash_upload.h`, `src/crash_upload.cpp`
- Create: `src/crash_payload.h`, `src/crash_payload.cpp`
- Modify: `scripts/symbolize_crash.py` (the `chip_id` correction, fact 5)

**Interfaces:**
- Consumes: `crash_url_host_allowed()`, `CRASH_BROKER_HOST`, `crash_redact_config()` (Task 6); `diagnostics_coredump_image()`, `diagnostics_coredump_json()` (`src/diagnostics.h`); `Mongoose.getMgr()`, `Mongoose.getDefaultOpts(&opts, true)`; `event_send()` (`src/event.h`).
- Produces: `crash_upload_begin()`, `crash_upload_loop()`, `crash_upload_request(String &msg)`, `crash_upload_state()`, `crash_upload_state_name()`, `crash_upload_sent()`, `crash_upload_total()`. Task 8 calls all of them.

Three requests, one sender. Every one of them goes over a **verified** TLS connection built by hand, for two reasons: `MongooseHttpClient` cannot carry a binary body at all (fact 1), and it does not verify certificates (fact 2). Rolling one small sender for all three is less code than mixing the two, and one code path to review.

The `report_id` that comes back from the metadata POST is attacker-controlled the moment the broker is. It is never concatenated into a path as received: it is validated as a UUID and the path is rebuilt locally (Review Focus 1).

- [ ] **Step 1: Write the failing test for the piece that can be tested on the host**

Create `test/test_crash_upload_id/test_crash_upload_id.cpp`:

```cpp
// Host-side tests for the one piece of the uploader that is pure: deciding
// whether the id a broker handed back may be built into a request path.
// Review Focus 1 -- the broker's reply is attacker-controlled once the broker
// is, and this id is the only part of it the device acts on.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "crash_report_id.h"

TEST_CASE("a real report id is accepted") {
  CHECK(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c3301"));
  CHECK(crash_report_id_valid("00000000-0000-0000-0000-000000000000"));
}

TEST_CASE("anything that could escape the path is refused") {
  CHECK_FALSE(crash_report_id_valid("../../../elf/firmware.elf"));
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c3301/.."));
  CHECK_FALSE(crash_report_id_valid("3f2504e0 4f89 41d3 9a0c 0305e82c3301"));
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c33\r\nX: y"));
  CHECK_FALSE(crash_report_id_valid("?X-Amz-Signature=abc"));
}

TEST_CASE("the wrong shape is refused, including near misses") {
  CHECK_FALSE(crash_report_id_valid(""));
  CHECK_FALSE(crash_report_id_valid(NULL));
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c330"));   // short
  CHECK_FALSE(crash_report_id_valid("3f2504e0-4f89-41d3-9a0c-0305e82c33011")); // long
  CHECK_FALSE(crash_report_id_valid("3F2504E0-4F89-41D3-9A0C-0305E82C3301"));  // upper
  CHECK_FALSE(crash_report_id_valid("3f2504e04f8941d39a0c0305e82c3301"));      // no dashes
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd /home/rar/oevse/openevse_esp32_firmware && scripts/pio test -e native_test -f test_crash_upload_id
```

Expected: FAIL at compile — `crash_report_id.h: No such file or directory`.

- [ ] **Step 3: Implement the id check**

Create `src/crash_report_id.h`:

```cpp
#ifndef CRASH_REPORT_ID_H
#define CRASH_REPORT_ID_H

// True if `id` is exactly a lower-case hyphenated UUID, and so may be built
// into a request path.
//
// The broker's reply is attacker-controlled the moment the broker is, and this
// id is the only part of it the device acts on. Rather than sanitising the
// path the broker suggests, the device validates the id and builds the path
// itself -- there is then no string from the network in the request line at
// all.
bool crash_report_id_valid(const char *id);

#endif // CRASH_REPORT_ID_H
```

Create `src/crash_report_id.cpp`:

```cpp
#include "crash_report_id.h"

#include <string.h>

// 8-4-4-4-12
static const int DASH_AT[] = { 8, 13, 18, 23 };

bool crash_report_id_valid(const char *id)
{
  if(!id || 36 != strlen(id)) {
    return false;
  }
  for(int i = 0; i < 36; i++) {
    bool dash = false;
    for(size_t d = 0; d < sizeof(DASH_AT) / sizeof(DASH_AT[0]); d++) {
      if(i == DASH_AT[d]) {
        dash = true;
        break;
      }
    }
    if(dash) {
      if('-' != id[i]) {
        return false;
      }
    } else if(!((id[i] >= '0' && id[i] <= '9') ||
                (id[i] >= 'a' && id[i] <= 'f'))) {
      // Lower case only. Accepting upper case would buy nothing and widen the
      // set of strings that reach a request line.
      return false;
    }
  }
  return true;
}
```

Add `+<crash_report_id.cpp>` to `[env:native_test]`'s `build_src_filter`.

- [ ] **Step 4: Run it to verify it passes**

```bash
cd /home/rar/oevse/openevse_esp32_firmware && scripts/pio test -e native_test
```

Expected: PASS, every suite.

- [ ] **Step 5: Write the metadata payload builder**

Create `src/crash_payload.h`:

```cpp
#ifndef CRASH_PAYLOAD_H
#define CRASH_PAYLOAD_H

#include <ArduinoJson.h>

// Build the metadata document the broker indexes (spec §5). Everything here is
// small and non-secret; the 64 KB image goes separately, and the config in it
// has been through crash_redact_config().
//
// `rawBytes` is how many bytes the device intends to PUT afterwards, or 0 for
// a summary-only report. The broker uses it to bound the upload before it
// happens, so it has to be the real figure.
void crash_payload_build(JsonDocument &doc, size_t rawBytes);

#endif // CRASH_PAYLOAD_H
```

Create `src/crash_payload.cpp`:

```cpp
#include "crash_payload.h"

#include "emonesp.h"          // buildenv, currentfirmware, serial
#include "app_config.h"
#include "diagnostics.h"
#include "crash_redact.h"
#include "evse_man.h"
#include <espal.h>

void crash_payload_build(JsonDocument &doc, size_t rawBytes)
{
  doc["version"] = currentfirmware;
  doc["buildenv"] = buildenv;

  // The ESP's own id, not evse.getChipId(). The controller's chip id is what
  // scripts/symbolize_crash.py sent, which collapses every unit with a fake or
  // absent controller onto one value and makes the by-chip index useless.
  doc["chip_id"] = serial;
  doc["espinfo"] = ESPAL.getChipInfo();
  // The controller's own firmware version (spec §5). Absent, not empty, when
  // no controller has answered -- an empty string here is indistinguishable
  // from "an OpenEVSE running version ''".
  const char *evseFw = evse.getFirmwareVersion();   // const char *, not String
  if(evseFw && '\0' != evseFw[0]) {
    doc["firmware"] = evseFw;
  }

  // The decoded summary: panic reason, faulting task, PC, backtrace,
  // elf_sha256. This is what gets symbolized; the raw image is for the
  // questions it cannot answer.
  JsonObject summary = doc.createNestedObject("summary");
  {
    DynamicJsonDocument sd(JSON_OBJECT_SIZE(12) + JSON_ARRAY_SIZE(16) + 640);
    diagnostics_coredump_json(sd);
    for(JsonPair kv : sd.as<JsonObject>()) {
      summary[kv.key()] = kv.value();
    }
    // Hoisted to the top level: it is the ELF lookup key, and the broker
    // reads it there.
    doc["elf_sha256"] = sd["elf_sha256"];
    doc["bt"] = sd["bt"];
  }

  JsonObject diag = doc.createNestedObject("diagnostics");
  {
    DynamicJsonDocument dd(1024);
    diagnostics_status(dd);
    for(JsonPair kv : dd.as<JsonObject>()) {
      diag[kv.key()] = kv.value();
    }
  }

  {
    // hideSecrets = true is belt and braces. crash_redact_config is what
    // actually keeps credentials out, because it names what may leave rather
    // than what may not (spec §5) -- but there is no reason to materialise
    // them in a document at all on the way past.
    DynamicJsonDocument cfg(4096);
    config_serialize(cfg, /*longNames*/ true, /*compactOutput*/ false,
                     /*hideSecrets*/ true);
    JsonObject redacted = doc.createNestedObject("config");
    crash_redact_config(cfg.as<JsonObjectConst>(), redacted);
  }

  if(rawBytes > 0) {
    doc["raw_bytes"] = (uint32_t)rawBytes;
  }
}
```

- [ ] **Step 6: Write the uploader header**

Create `src/crash_upload.h`:

```cpp
#ifndef CRASH_UPLOAD_H
#define CRASH_UPLOAD_H

#include <Arduino.h>

// One-click crash reporting (spec §3). The device POSTs a metadata document to
// the compiled-in broker, PUTs the 64 KB core dump straight out of its flash
// mapping, then POSTs a completion -- and erases the dump only after both the
// PUT and the completion answered 2xx (spec §7).
//
// Everything here is asynchronous. A synchronous network call on this path is
// the same shape as the MQTT DNS bug that tripped the 5 s task watchdog and
// became upstream #1252 (spec §6.4).

enum CrashUploadState {
  CrashUpload_Idle,
  CrashUpload_Metadata,
  CrashUpload_Raw,
  CrashUpload_Completing,
  CrashUpload_Done,
  CrashUpload_Failed,
  // Deferred to the next boot: the heap could not carry a TLS handshake now
  // (spec §6.1 tier 2). The click already happened; this is that one authorised
  // action finishing later, never a new one (spec §8).
  CrashUpload_Deferred,
};

void crash_upload_begin();
void crash_upload_loop();

// Start an upload, or explain why not. `message` is user-facing.
// Returns false when there is no dump, one is already running, or the feature
// is not built in.
bool crash_upload_request(String &message);

CrashUploadState crash_upload_state();
const char *crash_upload_state_name();
size_t crash_upload_sent();
size_t crash_upload_total();
// The user asked once and it is waiting for the next boot.
bool crash_upload_deferred_armed();
// Forget a deferred upload. The user changing their mind is the only caller.
void crash_upload_cancel_deferred();

#endif // CRASH_UPLOAD_H
```

- [ ] **Step 7: Implement the sender**

Create `src/crash_upload.cpp`. The whole file:

```cpp
#include "emonesp.h"
#include "crash_upload.h"

#if ENABLE_CRASH_UPLOAD

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MongooseCore.h>
#include <esp_heap_caps.h>

#include "crash_host.h"
#include "crash_payload.h"
#include "crash_report_id.h"
#include "diagnostics.h"
#include "event.h"
#include "net_manager.h"
#include "ota_url_allow.h"     // crash_url_host_allowed()

// Fed to the connection in slices as its buffer drains, rather than handed
// over whole: the point of diagnostics_coredump_image() is that the 64 KB
// never enters the heap, and a single mg_send() of the lot would copy all of
// it into the send buffer and undo that.
#define CRASH_CHUNK              1024
#define CRASH_HIGH_WATER         4096

// The whole exchange, not one request. A charger that cannot finish in a
// minute is one whose network is the problem, and holding a TLS session open
// past that costs more heap than it is worth.
#define CRASH_TIMEOUT_MS         60000

// Broker replies are a few hundred bytes. A reply that keeps growing is either
// a bug or a hostile endpoint filling the heap of the device it is talking to.
#define CRASH_MAX_REPLY          8192

// A TLS handshake needs tens of KB contiguous, and heap_largest collapses on a
// charger that has been up for weeks -- the documented reason OTA fails then
// and succeeds after a reboot. Below this, the upload defers to tier 2 rather
// than failing in the middle of a handshake.
//
// PROVISIONAL. Spec §13 leaves the figure open; it needs a controller-attached
// unit to measure, and the bench S3's fake controller cannot reproduce
// RAPI-driven fragmentation. Chosen to be safely above a handshake plus the
// send buffer, not measured.
#ifndef CRASH_MIN_HEAP_LARGEST
#define CRASH_MIN_HEAP_LARGEST   (48 * 1024)
#endif

// Set by a click, consumed once, never re-armed by firmware (spec §8).
//
// A LittleFS file, where spec §6.1 says NVS. Same durability, and a file is
// reachable from exactly one place in this firmware; nothing on master writes
// an arbitrary LittleFS path from a request. A config option -- the obvious
// alternative -- could be set by any authenticated /config write, which would
// make the consent invariant a comment rather than a property.
#define CRASH_DEFER_FLAG         "/crash_upload_pending"

static CrashUploadState _state = CrashUpload_Idle;
static String _reportId;
static const uint8_t *_image = NULL;
static size_t _imageLen = 0;
static size_t _sent = 0;
static size_t _reported = 0;       // last progress figure announced
static uint32_t _deadline = 0;
static bool _triedThisBoot = false;
// The flag was on disk when this boot started. Only such a flag is a deferred
// upload; one armed during this boot belongs to the next one.
static bool _armedAtBoot = false;

// ---------------------------------------------------------------------------
// One request, used three times.
// ---------------------------------------------------------------------------

struct CrashRequest {
  const char *method;
  String path;
  const char *contentType;
  const uint8_t *body;
  size_t bodyLen;
  size_t sent;
  bool headSent;
  String reply;
  int status;
};

static CrashRequest _req;
static mg_connection *_nc = NULL;

static void crash_fail(const char *why);
static void crash_reply(int status, const String &body);

static void crash_pump(mg_connection *nc)
{
  while(_req.sent < _req.bodyLen && nc->send_mbuf.len < CRASH_HIGH_WATER) {
    size_t chunk = _req.bodyLen - _req.sent;
    if(chunk > CRASH_CHUNK) {
      chunk = CRASH_CHUNK;
    }
    mg_send(nc, _req.body + _req.sent, chunk);
    _req.sent += chunk;
  }
  if(CrashUpload_Raw == _state) {
    _sent = _req.sent;
    // Announced every 8 KB rather than every slice: this runs from the
    // mongoose poll, and a websocket broadcast per kilobyte would cost more
    // than the upload.
    if(_sent - _reported >= 8192 || _sent == _imageLen) {
      _reported = _sent;
      DynamicJsonDocument doc(128);
      doc["crash_upload"] = crash_upload_state_name();
      doc["crash_upload_sent"] = (uint32_t)_sent;
      doc["crash_upload_total"] = (uint32_t)_imageLen;
      event_send(doc);
    }
  }
}

static void crash_ev_handler(struct mg_connection *nc, int ev, void *p, void *u)
{
  (void)u;
  // One request struct serves three successive connections. A straggling
  // MG_EV_SEND on a connection we have already moved past would otherwise pump
  // the NEXT request's body into the old socket.
  if(nc != _nc && MG_EV_CLOSE != ev) {
    return;
  }
  switch(ev)
  {
    case MG_EV_CONNECT:
      if(0 != *(int *)p) {
        crash_fail("connect failed");
        return;
      }
      // Request line and headers first, then the body in slices. Connection:
      // close because this is one request per connection and the broker's
      // reply is the end of it.
      mg_printf(nc,
                "%s %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: OpenEVSE\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                _req.method, _req.path.c_str(), CRASH_BROKER_HOST,
                _req.contentType, (unsigned)_req.bodyLen);
      _req.headSent = true;
      crash_pump(nc);
      break;

    case MG_EV_SEND:
    case MG_EV_POLL:
      if(_req.headSent) {
        crash_pump(nc);
      }
      break;

    case MG_EV_RECV:
      if(nc->recv_mbuf.len > CRASH_MAX_REPLY) {
        crash_fail("reply too large");
      }
      break;

    case MG_EV_HTTP_REPLY: {
      http_message *hm = (http_message *)p;
      String body;
      body.reserve(hm->body.len + 1);
      body.concat(hm->body.p, hm->body.len);
      int status = hm->resp_code;
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      _nc = NULL;
      crash_reply(status, body);
      break;
    }

    case MG_EV_CLOSE:
      if(_nc == nc) {
        _nc = NULL;
        crash_fail("closed before a reply");
      }
      break;
  }
}

// Start one request. `body` must outlive the request -- for the raw PUT that
// is the flash mapping, which is valid for the life of the boot.
static bool crash_send(const char *method, const String &path,
                       const char *contentType,
                       const uint8_t *body, size_t bodyLen)
{
  String url = String("https://") + CRASH_BROKER_HOST + path;
  // Spec §4, on every request rather than only the first: a compromised or
  // spoofed broker reply cannot move the image off this host.
  if(!crash_url_host_allowed(url.c_str())) {
    crash_fail("destination not allowed");
    return false;
  }

  _req.method = method;
  _req.path = path;
  _req.contentType = contentType;
  _req.body = body;
  _req.bodyLen = bodyLen;
  _req.sent = 0;
  _req.headSent = false;
  _req.status = 0;

  struct mg_connect_opts opts;
  // `true`: this asks for the real root CA bundle. MongooseHttpClient passes
  // false and mongoose then substitutes "*", which is no verification at all
  // (mongoose.c:8672) -- not something to inherit on a path that carries a
  // memory image.
  Mongoose.getDefaultOpts(&opts, true);
  const char *err = NULL;
  opts.error_string = &err;

  // "tcp://", NOT "ssl://" -- mg_parse_address strips only udp:// and tcp://
  // (mongoose.c:2648), so an ssl:// address fails to parse and mg_connect_opt
  // returns NULL with "cannot parse address". TLS is turned on by opts
  // .ssl_ca_cert being non-NULL, which is exactly what mg_connect_http_base
  // does (mongoose.c:8660). SNI and hostname verification come free: with a CA
  // set and no ssl_server_name, mongoose uses the DNS host it parsed
  // (mongoose.c:3158) and mbedtls_ssl_set_hostname checks the cert against it.
  String addr = String("tcp://") + CRASH_BROKER_HOST + ":443";
  // Asynchronous, including the DNS lookup: mongoose resolves through its own
  // resolver rather than getaddrinfo, so nothing here blocks loopTask
  // (spec §6.4).
  _nc = mg_connect_opt(Mongoose.getMgr(), addr.c_str(),
                       MG_CB(crash_ev_handler, NULL), opts);
  if(NULL == _nc) {
    crash_fail(err ? err : "connect failed");
    return false;
  }
  mg_set_protocol_http_websocket(_nc);
  return true;
}

// ---------------------------------------------------------------------------
// The three steps.
// ---------------------------------------------------------------------------

static String _metaBody;      // must outlive the request

static void crash_step_metadata()
{
  DynamicJsonDocument doc(6144);
  crash_payload_build(doc, _imageLen);
  _metaBody = "";
  serializeJson(doc, _metaBody);

  _state = CrashUpload_Metadata;
  crash_send("POST", "/v1/reports", "application/json",
             (const uint8_t *)_metaBody.c_str(), _metaBody.length());
}

static void crash_step_raw()
{
  _state = CrashUpload_Raw;
  _sent = 0;
  _reported = 0;
  crash_send("PUT", "/v1/reports/" + _reportId + "/raw",
             "application/octet-stream", _image, _imageLen);
}

static void crash_step_complete()
{
  _state = CrashUpload_Completing;
  crash_send("POST", "/v1/reports/" + _reportId + "/complete",
             "application/json", (const uint8_t *)"", 0);
}

static void crash_reply(int status, const String &body)
{
  switch(_state)
  {
    case CrashUpload_Metadata: {
      if(200 != status) {
        crash_fail("broker refused the report");
        return;
      }
      // Filtered, not parsed whole. The reply is small today because the
      // broker omits `frames` for a device (Task 1), but a filter makes that
      // a belt-and-braces property rather than a coupling: anything the broker
      // grows later is discarded before it can exhaust this document.
      StaticJsonDocument<64> filter;
      filter["report_id"] = true;
      StaticJsonDocument<128> doc;
      if(DeserializationError::Ok !=
         deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        crash_fail("bad reply");
        return;
      }
      const char *id = doc["report_id"];
      // Review Focus 1. The id is validated and the path is rebuilt from it;
      // the broker's own suggested path is never used, so no string from the
      // network reaches a request line.
      if(!crash_report_id_valid(id)) {
        crash_fail("bad report id");
        return;
      }
      _reportId = id;
      crash_step_raw();
      break;
    }

    case CrashUpload_Raw:
      if(200 != status) {
        crash_fail("dump refused");
        return;
      }
      crash_step_complete();
      break;

    case CrashUpload_Completing:
      if(200 != status) {
        crash_fail("completion refused");
        return;
      }
      // Spec §7: both the PUT and the completion answered 2xx, and only now is
      // erasing safe. A dump erased on a partial upload is unrecoverable.
      diagnostics_coredump_erase();
      _image = NULL;
      _state = CrashUpload_Done;
      {
        DynamicJsonDocument doc(128);
        doc["crash_upload"] = crash_upload_state_name();
        event_send(doc);
      }
      break;

    default:
      break;
  }
}

static void crash_fail(const char *why)
{
  DBUGF("crash upload failed: %s", why);
  if(_nc) {
    _nc->flags |= MG_F_CLOSE_IMMEDIATELY;
    _nc = NULL;
  }
  _state = CrashUpload_Failed;
  _image = NULL;
  DynamicJsonDocument doc(192);
  doc["crash_upload"] = crash_upload_state_name();
  doc["crash_upload_error"] = why;
  event_send(doc);
}

// ---------------------------------------------------------------------------
// Public surface.
// ---------------------------------------------------------------------------

static bool crash_running()
{
  return CrashUpload_Metadata == _state || CrashUpload_Raw == _state ||
         CrashUpload_Completing == _state;
}

static void crash_arm_deferred()
{
  File f = LittleFS.open(CRASH_DEFER_FLAG, "w");
  if(f) {
    f.print("1");
    f.close();
  }
  // Deferred means NEXT boot. Without this, crash_upload_loop() would see the
  // Deferred state on its very next pass, consume the flag and retry
  // immediately against the same starved heap that just failed the gate --
  // turning "will upload after the next restart" into a lie and losing the
  // flag in the process.
  _triedThisBoot = true;
  _state = CrashUpload_Deferred;
  DynamicJsonDocument doc(128);
  doc["crash_upload"] = crash_upload_state_name();
  event_send(doc);
}

bool crash_upload_deferred_armed()
{
  return LittleFS.exists(CRASH_DEFER_FLAG);
}

void crash_upload_cancel_deferred()
{
  LittleFS.remove(CRASH_DEFER_FLAG);
  _armedAtBoot = false;
  if(CrashUpload_Deferred == _state) {
    _state = CrashUpload_Idle;
  }
}

static bool crash_begin_now(String &message)
{
  if(!diagnostics_coredump_image(&_image, &_imageLen) || 0 == _imageLen) {
    message = F("no crash dump stored");
    return false;
  }
  _deadline = millis() + CRASH_TIMEOUT_MS;
  _triedThisBoot = true;
  crash_step_metadata();
  message = F("uploading");
  return true;
}

bool crash_upload_request(String &message)
{
  if(crash_running()) {
    // Two uploaders would share one mapping, one request struct and one
    // connection pointer. Refusing is the whole guard (Review Focus 3).
    message = F("an upload is already running");
    return false;
  }

  const uint8_t *img = NULL;
  size_t len = 0;
  if(!diagnostics_coredump_image(&img, &len) || 0 == len) {
    message = F("no crash dump stored");
    return false;
  }

  if(!net.isConnected()) {
    crash_arm_deferred();
    message = F("no network -- will upload after the next restart");
    return true;
  }

  uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if(largest < CRASH_MIN_HEAP_LARGEST) {
    // Spec §6.1 tier 2. Not a failure: the click stands, and the upload
    // completes on the next boot against a pristine heap. Telling the user it
    // worked would be a lie, and telling them it failed would make them think
    // the dump was lost.
    DBUGF("crash upload deferred, largest free block %u", largest);
    crash_arm_deferred();
    message = F("not enough contiguous memory -- will upload after the next restart");
    return true;
  }

  return crash_begin_now(message);
}

void crash_upload_begin()
{
  // Nothing here touches the network; tier 2 waits for connectivity in loop().
  if(crash_upload_deferred_armed()) {
    _armedAtBoot = true;
    _state = CrashUpload_Deferred;
  }
}

void crash_upload_loop()
{
  if(crash_running()) {
    if((long)(millis() - _deadline) >= 0) {
      crash_fail("timed out");
    }
    return;
  }

  if(CrashUpload_Deferred != _state || _triedThisBoot || !net.isConnected() ||
     !_armedAtBoot) {
    return;
  }

  // Spec §6.1 tier 2, and deviation D3: this runs at the first moment the
  // network is up, which races MQTT and OCPP rather than strictly preceding
  // them. The spec's "before MQTT connects" needs a hook inside the connect
  // path, and that is the code path #1252 came out of. Tier 3 (spec §6.3) is
  // the real answer and stays deferred.
  //
  // The flag is cleared BEFORE the attempt, not after: a crash mid-upload must
  // not boot back into uploading, which on a device that is not charging is a
  // loop. A failed attempt leaves the dump in place and the user can click
  // again.
  LittleFS.remove(CRASH_DEFER_FLAG);
  // Set before the attempt, not after. crash_begin_now() returns false when
  // the dump has since been erased, and without this the loop would call it --
  // and esp_core_dump_image_get(), a flash read -- on every pass for the rest
  // of the boot.
  _triedThisBoot = true;
  String message;
  if(!crash_begin_now(message)) {
    _state = CrashUpload_Idle;
  }
}

CrashUploadState crash_upload_state() { return _state; }
size_t crash_upload_sent() { return _sent; }
size_t crash_upload_total() { return _imageLen; }

const char *crash_upload_state_name()
{
  switch(_state) {
    case CrashUpload_Metadata:   return "metadata";
    case CrashUpload_Raw:        return "uploading";
    case CrashUpload_Completing: return "completing";
    case CrashUpload_Done:       return "done";
    case CrashUpload_Failed:     return "failed";
    case CrashUpload_Deferred:   return "deferred";
    default:                     return "idle";
  }
}

#else // ENABLE_CRASH_UPLOAD

// Built out on the 4 MB boards (spec §11). The callers stay unconditional so
// main.cpp and web_server.cpp do not grow a second set of #ifs.
void crash_upload_begin() {}
void crash_upload_loop() {}
bool crash_upload_request(String &message)
{
  message = F("not supported on this build");
  return false;
}
CrashUploadState crash_upload_state() { return CrashUpload_Idle; }
const char *crash_upload_state_name() { return "unsupported"; }
size_t crash_upload_sent() { return 0; }
size_t crash_upload_total() { return 0; }
bool crash_upload_deferred_armed() { return false; }
void crash_upload_cancel_deferred() {}

#endif // ENABLE_CRASH_UPLOAD
```

- [ ] **Step 8: Fix the CLI's device identity (fact 5)**

In `scripts/symbolize_crash.py`, change the `chip_id` line:

```python
        # The ESP's own id. config['chip_id'] is evse.getChipId() -- the
        # OpenEVSE controller's -- which collapses every unit with a fake or
        # absent controller onto one value and makes the by-chip index useless.
        'chip_id': config.get('wifi_serial', 'unknown'),
```

- [ ] **Step 9: Build it**

```bash
cd /home/rar/oevse/openevse_esp32_firmware && scripts/pio run -e openevse_wifi_v1_16mb
```

Expected: links. Record the flash figure from the output — Task 9 compares it against the baseline.

- [ ] **Step 10: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/crash_upload.h src/crash_upload.cpp src/crash_payload.h \
        src/crash_payload.cpp src/crash_report_id.h src/crash_report_id.cpp \
        scripts/symbolize_crash.py platformio.ini test/test_crash_upload_id
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: device-side crash report uploader"
```

---

### Task 8: The device endpoint, and the guard against erasing mid-upload

**Files:**
- Modify: `src/web_server.cpp`
- Modify: `src/main.cpp`
- Modify: `test/basic.http`

**Interfaces:**
- Consumes: everything Task 7 produced, plus `requestPreProcess()` and `diagnostics_coredump_image()`.
- Produces: `POST /debug/crash/upload`, `GET /debug/crash/upload`, `DELETE /debug/crash/upload`. The GUI (Task 10) consumes all three plus the `crash_upload` websocket events.

`requestPreProcess()` already carries the auth and the CSRF check that every other state-changing route relies on, so the endpoint gets both for free by using it — spec §3's "authenticated like other config writes" is that one call.

The guard matters more than it looks: `DELETE /debug/crash` erases the partition that the in-flight PUT is streaming out of. The mapping stays valid, so nothing faults — the upload simply completes, having sent 64 KB of `0xFF` that passes the broker's length check and is stored as a genuine memory image. Refusing the erase while an upload runs is the fix (Review Focus 2).

- [ ] **Step 1: Write the failing test**

Append to `test/basic.http`:

```
### Crash upload: state, with no dump stored
GET {{host}}/debug/crash/upload
Authorization: Basic {{auth}}

### Crash upload: start (404 when there is nothing to send)
POST {{host}}/debug/crash/upload
Authorization: Basic {{auth}}
X-Requested-With: OpenEVSE

### Crash upload: cancel a deferred upload
DELETE {{host}}/debug/crash/upload
Authorization: Basic {{auth}}
X-Requested-With: OpenEVSE
```

and run the route check against a live device:

```bash
cd /home/rar/oevse/openevse_esp32_firmware
curl -sS -o /dev/null -w '%{http_code}\n' http://10.75.1.6/debug/crash/upload
```

Expected: `404` — the route does not exist yet. (The device under test has no www password, so auth is off; on a password-protected charger add `-u`.)

- [ ] **Step 2: Implement the routes**

In `src/web_server.cpp`, after the `/debug/crash/raw$` handler, add:

```cpp
  // One-click crash reporting (spec §3).
  //
  //   GET    /debug/crash/upload  where an upload has got to
  //   POST   /debug/crash/upload  send the stored dump to the broker
  //   DELETE /debug/crash/upload  forget an upload deferred to the next boot
  //
  // requestPreProcess carries the auth and the CSRF check, so this is
  // authenticated exactly like a config write -- which is the bar a request
  // that ships a memory image off the device should clear.
  server.on("/debug/crash/upload$", [](MongooseHttpServerRequest *request) {
    MongooseHttpServerResponseStream *response;
    if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
      return;
    }

    if(HTTP_DELETE == request->method()) {
      // Spec §8: the user changing their mind is a first-class case, not an
      // edge case.
      crash_upload_cancel_deferred();
      response->setCode(200);
      response->print(F("{\"msg\":\"cancelled\"}"));
      request->send(response);
      return;
    }

    if(HTTP_POST == request->method()) {
      String message;
      bool ok = crash_upload_request(message);
      response->setCode(ok ? 200 : 409);
      DynamicJsonDocument doc(256);
      doc["msg"] = message;
      doc["state"] = crash_upload_state_name();
      doc["deferred"] = crash_upload_deferred_armed();
      serializeJson(doc, *response);
      request->send(response);
      return;
    }

    DynamicJsonDocument doc(256);
    doc["state"] = crash_upload_state_name();
    doc["sent"] = (uint32_t)crash_upload_sent();
    doc["total"] = (uint32_t)crash_upload_total();
    doc["deferred"] = crash_upload_deferred_armed();
    response->setCode(200);
    serializeJson(doc, *response);
    request->send(response);
  });
```

and add `#include "crash_upload.h"` to the file's includes.

- [ ] **Step 3: Guard the erase**

In the existing `/debug/crash$` handler, inside the `HTTP_DELETE` branch, before `diagnostics_coredump_erase()`:

```cpp
      // Erasing the partition an in-flight PUT is streaming out of does not
      // fault -- the flash mapping stays valid -- it just turns the rest of
      // the upload into 0xFF, which passes the broker's length check and is
      // then stored as a genuine memory image. Refuse instead.
      if(CrashUpload_Idle != crash_upload_state() &&
         CrashUpload_Done != crash_upload_state() &&
         CrashUpload_Failed != crash_upload_state() &&
         CrashUpload_Deferred != crash_upload_state()) {
        response->setCode(409);
        response->print(F("{\"msg\":\"upload in progress\"}"));
        request->send(response);
        return;
      }
      // Erasing while an upload is deferred also withdraws the deferral: the
      // dump it was queued to send is about to stop existing, and leaving the
      // flag armed means the next boot consumes it and finds nothing.
      crash_upload_cancel_deferred();
```

- [ ] **Step 4: Wire it into the main loop**

In `src/main.cpp`, add `#include "crash_upload.h"`, then in `setup()` after `LittleFS` and `Mongoose.begin()` are both up (next to `web_server_setup()`):

```cpp
  // Reads a LittleFS flag only; nothing here touches the network. A deferred
  // upload waits for connectivity in loop() (spec §6.1 tier 2).
  crash_upload_begin();
```

and in `loop()`, after `Mongoose.poll()` and beside `http_update_loop()`:

```cpp
  crash_upload_loop();
```

- [ ] **Step 5: Build and flash the bench unit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
scripts/pio run -e openevse_wifi_v1_16mb
# archive the ELF BEFORE anything relinks it -- the hash in the report is this
# file's, and an ELF copied at the wrong moment symbolizes to nothing
sha256sum .pio/build/openevse_wifi_v1_16mb/firmware.elf
cp .pio/build/openevse_wifi_v1_16mb/firmware.elf \
   /tmp/crash-upload-$(date +%Y%m%d).elf
```

CI has never archived an ELF (the secrets are unset, deliberately), so put this
build's ELF in the archive by hand or Task 9's run comes back `unsymbolized`
rather than `symbolized`:

```bash
ELF=.pio/build/openevse_wifi_v1_16mb/firmware.elf
aws s3 cp --region us-east-2 "$ELF" \
  "s3://crashstorage-crashbucketed041c25-mulnvmsdrfn4/elf/$(sha256sum "$ELF" | cut -d" " -f1)"
```

Flash the bench unit over USB, then:

```bash
curl -sS http://10.75.1.6/debug/crash/upload
```

Expected: `{"state":"idle","sent":0,"total":0,"deferred":false}`.

- [ ] **Step 6: Verify the erase guard and the double-click guard by reading the code paths**

Both need an upload in flight, which needs a stored dump, which Task 9 sets up. Record here that they are covered by Task 9 step 4 and do not claim them verified yet.

- [ ] **Step 7: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/web_server.cpp src/main.cpp test/basic.http
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: POST /debug/crash/upload, and refuse to erase mid-upload"
```

---

### Task 9: The build gate, the measured flash cost, and the end-to-end run

**Files:**
- Modify: `platformio.ini`
- Modify: `docs/user/troubleshooting.md`
- Modify: `docs/ai/feature-map.md`

**Interfaces:**
- Consumes: `ENABLE_CRASH_UPLOAD`, referenced by `src/crash_upload.cpp` (Task 7).
- Produces: nothing further tasks consume. This is the task that makes the claim "it fits" true rather than assumed.

Spec §11 says the flash cost is measured before it is claimed, and the user's decision is that the feature is on for any 16 MB board and off elsewhere. Default **off** rather than default on with opt-outs: `openevse_wifi_v1` sits at 90.0% and `esp32-c3-devkitc-02` at 90.4%, and a new env added later should not overflow because nobody remembered to add a `DISABLE_`.

- [ ] **Step 1: Record the baseline**

Measure on **one** tree, by toggling the flag — not against a second worktree.
Two worktrees differ for reasons unrelated to this feature:
`scripts/auto_fw_version.py` bakes `local_<branch>_<hash>` into the binary (a
detached-HEAD baseline gives a different-length string), and a local build
embeds `gui-nightshift/dist` when it exists and the committed `web_static`
when it does not. Either alone makes "byte-identical" a coin toss.

```bash
cd /home/rar/oevse/openevse_esp32_firmware
scripts/pio run -e openevse_wifi_v1_16mb 2>&1 | grep -E 'Flash:|RAM:'   # gate ON
```

Then with `-D ENABLE_CRASH_UPLOAD=1` removed from that env's `build_flags`,
rebuild and record the same two lines. The difference is the feature's cost and
the only figure worth quoting. Write both into the ledger.

- [ ] **Step 2: Add the gate**

In `platformio.ini`, add to `build_flags` of `[env:openevse_wifi_tft_v1]`, `[env:openevse_wifi_tft_v1_dev]` and `[env:openevse_wifi_v1_16mb]`:

```ini
  ; One-click crash reporting (spec §11). On for 16 MB boards only: the 4 MB
  ; envs sit above 90% of their app partition, and the default is off so a new
  ; env cannot overflow because nobody remembered to opt out.
  -D ENABLE_CRASH_UPLOAD=1
```

- [ ] **Step 3: Measure**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
for e in openevse_wifi_v1 openevse_wifi_v1_16mb; do
  echo "== $e"; scripts/pio run -e "$e" 2>&1 | grep -E 'Flash:|RAM:'
done
```

Expected: `openevse_wifi_v1` builds, and

```bash
nm -C .pio/build/openevse_wifi_v1/firmware.elf | grep crash_upload
```

lists only the stub symbols from the `#else` branch — that, not a size
comparison, is the proof the gate is off there. Record the 16 MB delta from
step 1. If it exceeds 12 KB, stop and say so in the ledger rather than trimming
quietly: the spec's promise was that the figure would be reported, not that it
would be small.

- [ ] **Step 4: The end-to-end run on real hardware**

This is the task's real verification, and it needs a stored core dump on the bench unit (10.75.1.6, USB-attached).

To induce one: the `POST /config` → `RapiSender::flush()` watchdog path found while validating Plan A panics this board within about five seconds, because it runs a fake controller whose `flush()` never returns in time. If that path no longer exists on master, flash a scratch build carrying a deliberate null dereference behind a temporary debug route over USB — **that build is never committed and never pushed**.

Then:

```bash
curl -sS http://10.75.1.6/debug/crash | head -c 400          # a dump is stored
curl -sS -X POST http://10.75.1.6/debug/crash/upload \
     -H 'X-Requested-With: OpenEVSE'
```

Expected: `{"msg":"uploading","state":"metadata","deferred":false}`, then within a few seconds:

```bash
curl -sS http://10.75.1.6/debug/crash/upload
```

Expected: `{"state":"done","sent":65536,"total":65536,"deferred":false}`, and

```bash
curl -sS http://10.75.1.6/debug/crash
```

Expected: `{"present":false}` — spec §7's erase, which happened only after both 2xx.

Confirm the far end has it, and that it is symbolized:

```bash
aws dynamodb scan --region us-east-2 --table-name <CrashStorage table> \
  --max-items 5 --query 'Items[].{id:report_id.S,st:status.S,raw:raw.S,chip:chip_id.S}'
```

Expected: a row with `status: symbolized`, `raw: stored`, and `chip` equal to the ESP's long id (not the controller's), plus the object in S3 at exactly 65536 bytes.

- [ ] **Step 5: Verify the two guards Task 8 could not**

With a dump stored and an upload started:

```bash
curl -sS -X POST http://10.75.1.6/debug/crash/upload -H 'X-Requested-With: OpenEVSE' &
sleep 0.2
curl -sS -o /dev/null -w 'second-click=%{http_code}\n' \
     -X POST http://10.75.1.6/debug/crash/upload -H 'X-Requested-With: OpenEVSE'
curl -sS -o /dev/null -w 'erase=%{http_code}\n' \
     -X DELETE http://10.75.1.6/debug/crash -H 'X-Requested-With: OpenEVSE'
```

Expected: `second-click=409` (Review Focus 3) and `erase=409` (Review Focus 2).

- [ ] **Step 6: Check the deferred path**

Temporarily raise the bar so tier 1 always refuses, and confirm the click defers rather than failing:

```bash
PLATFORMIO_BUILD_FLAGS="-D CRASH_MIN_HEAP_LARGEST=0x7FFFFFFF" \
  scripts/pio run -e openevse_wifi_v1_16mb
```

Flash it, induce a dump, click, and expect the POST to answer 200 with `"deferred":true` and a message naming the restart. Reboot and confirm the upload completes on its own with nobody pressing anything — and that `/crash_upload_pending` is gone afterwards. Then reflash the normal build.

- [ ] **Step 7: Document it**

In `docs/user/troubleshooting.md` (the existing `/debug/crash` copy is at lines 47-56; there is no `developer_tools.md` on this branch), under the crash section, add what the button does, that the image contains credentials, that the dump is erased only on success, and that on a long-uptime charger the upload may defer to the next restart. In `docs/ai/feature-map.md`, add the three routes and `src/crash_upload.cpp`. Then:

```bash
cd /home/rar/oevse/openevse_esp32_firmware && python scripts/docs_coverage.py --strict
```

Expected: exit 0. It fails on an undocumented route, which is the point of running it here.

- [ ] **Step 8: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add platformio.ini docs/user/troubleshooting.md docs/ai/feature-map.md
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: enable crash upload on 16MB boards, and document it"
```

---

### Task 10: The button

**Repo:** the `gui-nightshift` submodule — **a separate repository and a separate pull request**. Do not bump the submodule pointer or regenerate `src/web_static/` as part of this plan.

**Files:**
- Modify: `src/routes/settings/Terminal.svelte`
- Modify: `src/lib/i18n/en.json`, `es.json`, `fr.json`, `hu.json`

**Interfaces:**
- Consumes: `POST|GET|DELETE /debug/crash/upload` and the `crash_upload`, `crash_upload_sent`, `crash_upload_total`, `crash_upload_error` websocket fields (Tasks 7 and 8).
- Produces: nothing downstream.

The crash block in `Terminal.svelte` already renders the summary, links the raw image and guards the erase behind a confirmation — the upload button sits beside them and reuses the same confirm shape. Two things it must not get wrong: the confirmation has to say plainly that this is a memory image that may carry the WiFi password, and a deferred result has to read as "not yet", never as success.

- [ ] **Step 1: Write the failing test**

Create `src/lib/i18n/__tests__/crash-upload-keys.test.js`:

```javascript
import { describe, it, expect } from 'vitest'
import en from '../en.json'
import es from '../es.json'
import fr from '../fr.json'
import hu from '../hu.json'

const KEYS = [
  'upload', 'upload_confirm_title', 'upload_confirm_body',
  'upload_progress', 'upload_done', 'upload_failed',
  'upload_deferred', 'upload_cancel_deferred',
]

describe('crash upload strings', () => {
  it.each([['es', es], ['fr', fr], ['hu', hu]])(
    '%s has every key en has', (_name, locale) => {
      for (const k of KEYS) {
        expect(en.config.terminal.crash[k]).toBeTruthy()
        expect(locale.config.terminal.crash[k]).toBeTruthy()
      }
    })

  it('the confirmation names the credentials, in every locale', () => {
    // A user clicking this is sending their WiFi PSK and every stored token to
    // a third party. Consent that does not say so is not consent (spec §8).
    for (const l of [en, es, fr, hu]) {
      expect(l.config.terminal.crash.upload_confirm_body.length)
        .toBeGreaterThan(40)
    }
  })
})
```

- [ ] **Step 2: Run it to verify it fails**

```bash
bash -lc 'source ~/.nvm/nvm.sh; nvm use 22 >/dev/null; cd /home/rar/oevse/openevse_esp32_firmware/gui-nightshift && npm test -- crash-upload-keys'
```

Expected: FAIL — `Cannot read properties of undefined`.

- [ ] **Step 3: Add the strings**

In `src/lib/i18n/en.json`, under `config.terminal.crash`:

```json
"upload": "Send to OpenEVSE",
"upload_confirm_title": "Send this crash report?",
"upload_confirm_body": "The crash report includes a 64 KB image of the charger's memory. That image can contain your Wi-Fi password, MQTT password and any stored access tokens. It is sent to OpenEVSE over an encrypted connection, kept privately for 90 days, and then deleted. The report is removed from this charger once it has been sent.",
"upload_progress": "Sending… {sent} of {total} KB",
"upload_done": "Sent. The crash report has been removed from this charger.",
"upload_failed": "Could not send the report. Nothing was removed from this charger — you can try again.",
"upload_deferred": "Not sent yet. This charger does not have enough free memory right now; the report will be sent automatically after the next restart.",
"upload_cancel_deferred": "Don't send it"
```

and translations of the same in `es.json`, `fr.json` and `hu.json`. Match the neighbouring keys' tone in each file; do not leave English strings in the other three.

- [ ] **Step 4: Run the test to verify it passes**

```bash
bash -lc 'source ~/.nvm/nvm.sh; nvm use 22 >/dev/null; cd /home/rar/oevse/openevse_esp32_firmware/gui-nightshift && npm test'
```

Expected: PASS, the whole vitest suite.

- [ ] **Step 5: Add the button and the progress**

In `Terminal.svelte`, beside the existing `crash` state:

```javascript
  // ── Crash report upload (spec §3) ───────────────────────────────────────
  // One click, three device-side steps; the device reports progress on the
  // websocket, so this only has to render what arrives. A deferred result is
  // NOT a success: the dump is still on the charger and the user needs to know
  // that a restart is what sends it.
  let uploadState = $state('idle')
  let uploadSent = $state(0)
  let uploadTotal = $state(0)
  let uploadError = $state('')
  let uploadDeferred = $state(false)
  let pendingUpload = $state(false)   // confirmation dialog open

  async function loadUploadState() {
    const res = await serialQueue.add(() => httpAPI('GET', '/debug/crash/upload'))
    if (res && res !== 'error') {
      uploadState = res.state
      uploadSent = res.sent
      uploadTotal = res.total
      uploadDeferred = res.deferred
    }
  }

  async function startUpload() {
    pendingUpload = false
    const res = await serialQueue.add(() =>
      httpAPI('POST', '/debug/crash/upload'))
    if (!res || res === 'error') {
      uploadState = 'failed'
      return
    }
    uploadState = res.state
    uploadDeferred = res.deferred
    // The dump is gone from the device only once the upload actually finished,
    // so refresh the summary rather than assuming either way.
    if (res.state === 'done') loadCrash()
  }

  async function cancelDeferred() {
    await serialQueue.add(() => httpAPI('DELETE', '/debug/crash/upload'))
    uploadDeferred = false
    uploadState = 'idle'
  }
```

Subscribe to the device event stream the same way the rest of this page does (follow the existing `event`/websocket subscription in `Terminal.svelte` or its parent) and map `crash_upload`, `crash_upload_sent`, `crash_upload_total` and `crash_upload_error` onto the four pieces of state above. Call `loadUploadState()` from the existing `onMount`, beside `loadCrash()`.

In the markup, inside the existing `{#if crash?.present}` block, after the erase control: the **Send to OpenEVSE** button opening the confirm dialog; the dialog itself using `upload_confirm_title` / `upload_confirm_body` in the same shape as the erase confirmation; a progress line while `uploadState` is `metadata`, `uploading` or `completing`; and — when `uploadDeferred` — the `upload_deferred` message with a `upload_cancel_deferred` control calling `cancelDeferred()`. Tone the deferred message as a warning, not a success.

- [ ] **Step 6: Build, test and screenshot**

```bash
bash -lc 'source ~/.nvm/nvm.sh; nvm use 22 >/dev/null; cd /home/rar/oevse/openevse_esp32_firmware/gui-nightshift && npm run build && npm test && npm run screenshots'
cd /home/rar/oevse/openevse_esp32_firmware && python scripts/sync_screenshots.py
```

Expected: build and tests pass; the screenshot sync reports the Terminal page updated. CI enforces that sync, so a skipped run fails the PR.

- [ ] **Step 7: Commit, in the submodule**

```bash
cd /home/rar/oevse/openevse_esp32_firmware/gui-nightshift
git add src/routes/settings/Terminal.svelte src/lib/i18n
git -c user.name="Andrew Rankin" -c user.email="andrew@eiknet.com" \
  commit -m "feat: send a crash report to OpenEVSE from Developer Tools"
```

Do **not** commit the regenerated screenshots into the firmware repo as part of this plan unless `sync_screenshots.py` changed files there; if it did, that is a separate firmware commit alongside the submodule pointer bump, which happens when the GUI PR merges — not here.

---

## Sequencing and what is blocked

Tasks 1-4 are independent of everything else and are the right place to start: they extend a stack that is already deployed and can be exercised with `curl` alone.

The firmware half branches off **`feat/crash-elf-archive`**, not master — Plan A's `scripts/symbolize_crash.py` and CI ELF step live only there and are still unpushed. Rebase that branch onto `oe-ssh/master` before cutting `feat/crash-upload` from it.

Task 5 is **not** a blocker for anything. `crash.openevse.com` is not delegated to this account, and the domain stack is built so that absence is a configuration, not a failure. The firmware pins the hostname from day one and bench work uses `-D CRASH_BROKER_HOST=...` against the generated API hostname; no firmware change waits on DNS. What *is* blocked on DNS is the final production validation — a charger built with the default host cannot reach the broker until the record exists — and that is one bench run, not a task.

Two open items from the spec stay open, deliberately:

- **The `CRASH_MIN_HEAP_LARGEST` threshold** (spec §13) is a provisional 48 KB. Measuring it needs a controller-attached unit and is blocked on the garage EVSE replacement. Task 9 step 6 tests the *mechanism* by forcing the threshold, which is the part that can be wrong; the number itself only decides how often tier 2 is used.
- **Tier 3, the dedicated upload boot mode** (spec §6.3), stays unbuilt. Nothing here forecloses it: the flag it needs exists after Task 7.

One thing this plan surfaced that belongs to neither: **outbound HTTPS in this firmware does not verify server certificates** (fact 2), which includes the OTA firmware download. This plan's uploader is not affected — it asks for the bundle explicitly — but the OTA path should be raised upstream separately.
