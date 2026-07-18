# ws_auth branch changes

Fixes a **missing-authentication** flaw in the ESP32 web server: three
WebSocket endpoints streamed device data without any credential check, even
when HTTP Basic auth was configured and correctly enforced on every REST route.
Also hardens the Basic-auth comparison against a timing side channel.

Branched from `master`. Verified against a live station (`openevse-c620`,
`openevse_wifi_v1_16mb`).

Source of the report: coordinated-disclosure advisory
"Unauthenticated WebSocket Endpoints Expose Device Telemetry, RFID Credentials,
and Serial Console Streams" (CWE-306 / CWE-200 primary, CWE-208 secondary).

## Background / the problem

Every REST handler runs through `requestPreProcess()`, which performs the HTTP
Basic-auth check. The three WebSocket endpoints registered only
`onFrame()` / `onConnect()` callbacks and therefore never passed through that
helper. In the underlying `ArduinoMongoose 0.0.26` dispatch, the WebSocket
handshake shares its branch with ordinary HTTP requests and the application
auth hook is only invoked when an `onRequest` handler is set — for a
WebSocket-only route it was not, so the handshake completed unauthenticated.

Result: any host able to reach the device's HTTP port could complete a
WebSocket handshake with **no credentials** and passively receive:

| Endpoint | Data exposed |
|---|---|
| `/ws` | Full device status on connect + all broadcast events, **including RFID card UIDs at enrollment** |
| `/evse/console` | Live RAPI serial TX/RX stream to/from the EVSE controller |
| `/debug/console` | Internal firmware debug log stream |

The RFID UID exposure is the highest-impact element: RFID authorization in this
firmware is a plaintext UID equality check (`rfid.cpp`), so a captured UID is
directly replayable to gain charging authorization.

Secondary: `MongooseHttpServerRequest::authenticate()` compares credentials with
`strcmp()`, which short-circuits on the first differing byte and is therefore
not constant-time (CWE-208). Low severity — network jitter dominates the
per-byte delta — but it lives in the same auth path.

## Fixes (all in `src/web_server.cpp`)

### 1. Authenticate the WebSocket handshake

A single gate function is registered via `->onRequest()` on all three WebSocket
endpoints:

```c
void onWsAuthenticate(MongooseHttpServerRequest *request)
{
  if(!isAuthenticated(request)) {
    request->requestAuthentication(esp_hostname);
  }
}
```

Because `onRequest` runs during `MG_EV_WEBSOCKET_HANDSHAKE_REQUEST` — **before**
the `101 Switching Protocols` response — a failed check calls
`requestAuthentication()`, which sends a `401` and sets `MG_F_SEND_AND_CLOSE`.
Mongoose then skips handshake completion entirely (`mongoose.c`, the
`MG_EV_WEBSOCKET_HANDSHAKE_REQUEST` handler only sends the 101 and fires
`HANDSHAKE_DONE` when the connection is not flagged for close). No `onConnect`
fires and no data is ever pushed to an unauthenticated client.

**Why the handshake-request stage, not `onConnect`:** the advisory's suggested
`onConnect` gate fires at `HANDSHAKE_DONE` (after the 101), the
`MongooseHttpWebSocketConnection` class in this library version has no `close()`
method, and the two console endpoints have no `onConnect` handler at all.
Gating at the request stage is one uniform mechanism that covers all three
endpoints before any data flows.

### 2. Single source of truth for auth + constant-time comparison

The Basic-auth check was factored out of `requestPreProcess()` into a shared
`isAuthenticated()` used by both the REST path and the WebSocket gate:

```c
bool isAuthenticated(MongooseHttpServerRequest *request)
{
  if(net.isWifiModeApOnly() || www_username == "") {
    return true;                       // AP provisioning / no admin password set
  }
  MongooseString authHeader = request->headers("Authorization");
  if(!authHeader) {
    return false;
  }
  mg_str hdr = authHeader.toMgStr();
  char user_buf[64] = {0};
  char pass_buf[64] = {0};
  if(0 != mg_parse_http_basic_auth(&hdr, user_buf, sizeof(user_buf),
                                   pass_buf, sizeof(pass_buf))) {
    return false;
  }
  return credentialsMatch(www_username.c_str(), user_buf) &&
         credentialsMatch(www_password.c_str(), pass_buf);
}
```

`credentialsMatch()` compares full length without an early exit, folding in the
length difference, so the check no longer leaks how many leading bytes matched
(CWE-208). Parsing reuses the library's tested `mg_parse_http_basic_auth()`;
only the comparison changed. The buffers are zero-initialised (the library
original could leave `pass` uninitialised if the header had no colon).

The `strcmp()` itself lives in `ArduinoMongoose`, a **PlatformIO registry
dependency** (`jeremypoulter/ArduinoMongoose@0.0.26`) that cannot be patched
persistently in this repo — so the fix was applied at the firmware layer, where
the firmware no longer routes credential comparison through the library's
`strcmp`. A constant-time fix upstream in ArduinoMongoose remains worthwhile.

## Not changed, and why

- **RFID UID broadcast (advisory §6.2, defence-in-depth).** Not implemented.
  Masking the enrolled UID would break the "Add RFID" workflow — the enrolling
  admin's browser needs the full UID to save the card — and per-connection
  delivery is not feasible because enrollment is triggered by a stateless REST
  call (`/rfid/add`) with no associated WebSocket. After fix #1 this data is
  already restricted to authenticated clients; limiting it to only the enrolling
  session is a GUI/product decision.

## Compatibility notes

- **The GUI keeps working.** Browsers automatically resend cached HTTP Basic
  credentials on same-origin requests, including the WebSocket handshake
  (`GET` upgrade). This is distinct from the JS `WebSocket` API's inability to
  set *custom* headers, and is confirmed on-device (see below).
- **Unprovisioned devices are unaffected.** With no admin password set (or in
  AP-only provisioning mode) `isAuthenticated()` returns `true`, so WebSockets
  stay open exactly as before.

## Testing

Build: `pio run -e openevse_wifi_v1` and `-e openevse_wifi_v1_16mb` both clean.

On-device (`openevse-c620`, live), a temporary admin password was set, the
before/after captured, then the password cleared to restore the original open
state:

| Test (admin password set) | Before fix | After fix |
|---|---|---|
| REST `/status`, no creds | 401 | 401 |
| WS `/ws`, no creds | **101 + full status leak** | **401** |
| WS `/evse/console`, no creds | **101** | **401** |
| WS `/debug/console`, no creds | **101** | **401** |
| WS `/ws`, valid creds | — | 101 + status data |
| WS `/evse/console` + `/debug/console`, valid creds | — | 101 |
| WS `/ws`, wrong user or wrong password | — | 401 |

With the password cleared, `/ws` returns to `101` (open) — confirming no
regression for unprovisioned/AP-provisioning devices.
