# NTP Status UI — Change Summary

**Firmware branch:** `NTP_Fixes` (`openevse_esp32_firmware`)
**GUI branch:** `NTP_fixes` (`openevse-gui-nightshift`)
**Date:** 2026-06-17

---

## Problem

After the first successful NTP sync, clicking **Update Now** never worked — only a full reboot re-synced the clock. The Time settings page also showed no indication of DNS resolution status, making it impossible to tell whether a hostname problem or a server problem was the cause of a sync failure.

---

## Root Cause: MongooseSntpClient Stale `_nc` Bug

`MG_EV_CLOSE` never fires for UDP SNTP connections in Mongoose 6. After the first sync, `_nc` (the internal connection pointer) stays permanently non-NULL. Every subsequent `getTime()` call checks `if(NULL == _nc)` and returns `false` without sending any traffic — silently doing nothing.

The `scripts/extra_script.py` patch was intended to fix this at build time, but was **never being applied** because the library source has a trailing space on the `if(NULL == _nc) ` line that the exact-string match didn't account for.

---

## Firmware Changes (`openevse_esp32_firmware`)

### `scripts/extra_script.py`

**Bug fixed:** `OLD_GET_TIME` did not match the library source because of a trailing space on `if(NULL == _nc) `. The `content not in` guard always returned `True`, so the patch was silently skipped every build.

**Fix:** Normalise each line before matching:
```python
content = "\n".join(line.rstrip() for line in original.splitlines())
```

This fix applies to all CI builds. PlatformIO installs fresh (unpatched) library files to `.pio/libdeps/<env>/` before running extra_scripts; the patch now correctly transforms them before compilation.

### `MongooseSntpClient.cpp` (patched by `extra_script.py` at build time)

Two fixes in the patched version:

**1. `getTime()` — force-close stale connection:**
```cpp
if(_nc != NULL) {
    _nc->flags |= MG_F_CLOSE_IMMEDIATELY;
    _nc = NULL;
}
```

**2. `MG_EV_CLOSE` — guard against clobbering a new connection:**
```cpp
case MG_EV_CLOSE:
    if(_nc == nc) { _nc = NULL; }  // only clear if this is still our connection
    break;
```

### `src/time_man.h`

- Added `bool _syncRequested` — set by `checkNow()` so `getNtpStatus()` immediately returns `"connecting"` before the MicroTask loop starts the fetch
- Added `char _resolvedIp[46]` — stores the resolved server IP, `"failed"` on DNS failure, or `""` when unknown
- Added `getResolvedIp()` accessor
- `checkNow()` now clears `_resolvedIp[0]` and sets `_syncRequested = true`

### `src/time_man.cpp`

| Change | Detail |
|---|---|
| `getNtpStatus()` | Returns `"connecting"` when `_syncRequested` is set — prevents false `"synchronized"` while update is pending |
| `getNextSyncMs()` | Returns `0` when `_syncRequested` is set — hides countdown during pending update |
| `onError` callback | Calls `getaddrinfo()` after error — shows resolved IP if DNS worked (NTP server issue), shows `"failed"` only if DNS itself failed |
| Early DNS probe | Task wakes 1 s after `getTime()` starts; calls `getaddrinfo()` which hits LwIP's DNS cache (Mongoose already resolved the hostname to send the UDP packet) — populates `_resolvedIp` while still in `"connecting"` state |
| `getTime()` returns false | Now increments `_retryCount` and shows `"retry"` status instead of silently maintaining `"synchronized"` |

**Status flow after Update Now:**

```
0 ms    checkNow() → _syncRequested=true, _resolvedIp=""
        → status: connecting, badge: none

~100 ms getTime() called, Mongoose resolves DNS and sends UDP
1000 ms task wakes → getaddrinfo() hits LwIP cache → _resolvedIp = "1.2.3.4"
        → status: connecting, badge: "DNS resolved to: 1.2.3.4"

~1-5 s  SNTP reply arrives → success callback
        → status: synchronized, badge: IP address
```

### `src/web_server_time.cpp`

`GET /time` now includes NTP status fields:

```json
{
  "ntp_status": "connecting | synchronized | retry | waiting | disabled",
  "ntp_last_sync": 1718000000,
  "ntp_next_sync_ms": 28800000,
  "ntp_server_ip": "185.96.2.100"
}
```

`ntp_server_ip` is omitted when status is unknown; set to `"failed"` when DNS resolution failed.

---

## GUI Changes (`openevse-gui-nightshift`)

### `src/routes/settings/Time.svelte`

**NTP status card** added at the bottom of the Time settings page (below timezone).

| Element | Detail |
|---|---|
| Status badge | Color-coded: Disabled=grey, Waiting=yellow, Connecting=blue, Synchronized=green ✓, Retry=red |
| Last sync | Relative time ("5m 12s ago") |
| Next sync / Next retry | Live countdown updated every second |
| Update Now button | POSTs `{sync_now: true}` to `/time`, clears DNS badge immediately, polls every 500 ms for up to 25 polls (~12 s) |
| DNS badge | Below hostname field — green "DNS resolved to: x.x.x.x" or red "DNS failed"; hidden when unknown |
| DNS badge clearing | Clears on Update Now press and on hostname change |
| Device time | Separate Date and Time rows (was a single row) |

### `src/lib/i18n/en.json`

New keys under `config.time`:

```
ntp_status_title, ntp_status_label, ntp_last_sync, ntp_next_sync, ntp_next_retry,
ntp_sync_now, ntp_syncing, ntp_never,
ntp_disabled, ntp_waiting, ntp_connecting, ntp_synchronized, ntp_retry,
ntp_dns_ok ("DNS resolved to: "), ntp_dns_failed ("DNS failed"),
device_date
```

New key under `units`: `"hz": "Hz"`

### `dev/mock-plugin.js`

Added `GET /api/time` and `POST /api/time` handlers for local dev mode (`npm run dev:mock`), returning a mock NTP status with `ntp_server_ip: '185.96.2.100'`.

---

## DNS Status Logic

| Scenario | Badge | Status |
|---|---|---|
| Update Now pressed | None (cleared) | Connecting |
| DNS resolved, SNTP in flight | IP address (green) | Connecting |
| DNS resolved, SNTP succeeded | IP address (green) | Synchronized |
| DNS resolved, SNTP failed | IP address (green) | Retry |
| DNS failed | DNS failed (red) | Retry |
| `getTime()` couldn't start (stale `_nc`) | None | Retry |
