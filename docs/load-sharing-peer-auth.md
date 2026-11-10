# Load sharing peer authentication

Design note for [#1172](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/1172):
load sharing stopped working as soon as `www_password` was set on a peer, because
peers sent no credential to the now-gated `/status`, `/config` and `/ws`.

## What was built

Every charger in a group holds the same `loadsharing_group_key`. The key signs
peer requests and is never sent.

1. The client calls `GET /loadsharing/nonce`, which needs no authentication. The
   server returns a random single-use nonce, valid for 15 seconds. It returns
   404 unless the server has both `www_password` and `loadsharing_group_key`
   set; the client then sends the request unsigned, which only a device with no
   password accepts.
2. The client computes `HMAC-SHA256(key, message)`, where the message is:

   ```text
   oevse-loadsharing-v1\n<METHOD>\n<path>\n<nonce>\n<sha256(body) as hex>
   ```

3. The client sends `Authorization: OpenEVSE-Peer v1.<nonce>.<mac>` on the real
   request. For `/ws` that header goes on the upgrade.
4. The server checks the route is in scope, verifies the signature, then spends
   the nonce.

The credential costs one extra round trip per request. Peer requests are rare:
bootstrap, identity fetch, config push, and each WebSocket connect. Allocations
and status then flow over the open socket, so nothing is signed per frame.

| File | Role |
|---|---|
| `src/loadsharing_auth.*` | Signing, verification, scope, nonce store. No Arduino types, so it is native tested |
| `src/loadsharing_peer_client.*` | Client side: fetch a nonce, sign, hand back the header |
| `src/web_server_loadsharing.cpp` | Nonce endpoint and `loadSharingPeerAuthorised()` |
| `src/web_server.cpp` | `isAuthenticated()` calls the above before Basic auth |
| `src/web_server_config.cpp` | Restricts what a signed `POST /config` may set |

## Why this and not the alternatives

| Option | Verdict |
|---|---|
| Admin credential (`www_password`) | Rejected. It forces one password across the group and puts it on the wire for every request |
| Group key sent as a bearer token | Rejected. Peers use plain HTTP, so one sniffed request gives the key away for good |
| Timestamped token (reusing `session_token_mint`) | Rejected. It needs a synced clock, so a site that lost internet after a power cut would stay in failsafe until SNTP returned |
| `server_secret` session token | Rejected. That secret is per device, so a peer cannot verify it without sharing it, and it rotates when credentials change |
| Client certificates (mTLS) | Stronger in principle, but not practical here. See below |
| **Group key with a signed nonce** | Chosen. No clock needed, the key stays off the wire, replays fail, and it reuses `crypto/hmac_sha256` |

### Client certificates

mTLS would also encrypt traffic and allow per-device revocation, which a shared
key cannot. The foundation for it is only partly there.

| Needed | State today |
|---|---|
| TLS on peer traffic | Peers default to `http://`, and `www_https_enabled` defaults to off |
| Server verifying client certificates | Mongoose's built-in TLS supports it (`is_twoway`), but `MongooseSocket::onAccept()` passes no `ca`, so it is never requested |
| Certificate identity reaching handlers | Not exposed to request handlers, so a route could not tell a peer from a browser |
| Client trusting a peer's server certificate | Certificates are self-signed per device with no shared root, and clients verify by default |
| Client presenting a certificate | Supported by `MongooseSocket::setCertificate()` |
| Issuing and distributing certificates | Missing. `self_signed_cert.cpp` makes one self-signed certificate, with no CA or signing flow |

Distribution is the deciding problem. A group CA needs its private key on
whichever charger signs. That means copying the key around, or building an
enrolment flow that must itself authenticate the joining charger, so it needs a
shared secret anyway. Expiry would also bring back the clock dependency above.
Each live peer socket would add a TLS session on a device already tight on heap.

If peer traffic ever needs confidentiality, the realistic path is enabling
HTTPS with pinned peer certificates, enrolled using this group key.

## Security properties

A signed request reaches only `GET /status`, `GET /config`, `GET /ws`,
`POST /loadsharing/peers`, and the fields the controller actually pushes through
`POST /config`. The group key is deliberately not an admin credential.

It cannot change `www_password`, the group key, the site budget or factory
settings, restart the device, reach `/update`, or read `/debug/*`.

Protects against:

- a passive LAN observer learning the key, which allows only an offline guess,
  hence the 16 character minimum
- replaying a captured request, or swapping its method, path or body
- a peer that holds the key taking over the device

Does not protect against:

- **Confidentiality.** Peer traffic is still plain HTTP, so status, config and
  allocations can be read on the LAN.
- **An active attacker on the network path.** Only the request is signed. A
  response or an established WebSocket could be tampered with after the
  handshake, and a false allocation could reach a member that way.
- **A compromised group member.** The key is shared, so every holder can act as
  the controller, and one charger cannot be revoked on its own. Rotate the key
  on all of them instead.
- **Nonce flooding.** `/loadsharing/nonce` is open, so a LAN client can keep
  evicting nonces. The store is a fixed eight slots, so this costs peers retries
  rather than memory, but a sustained flood can keep a group in failsafe. A
  per-client cap would blunt it, but ArduinoMongoose 1.2.0 does not expose the
  remote address of an HTTP request (only of a WebSocket connection), so it
  needs a library change first.

Devices without a `www_password` stay open exactly as before.

## Behaviour to know about

- **The key is entered on every charger by hand.** The controller cannot deliver
  it: it has no credential yet, and sending it over HTTP would defeat the design.
  This is why the field stays writable on a member, unlike other load sharing
  settings, and why the controller never pushes it.
- **Failed signatures do not feed the login throttle.** A misconfigured peer
  retries forever and must not lock the owner out of the UI. A wrong key shows as
  `error: "auth_failed"` on `GET /loadsharing/status`.
- **Rotating the key does not drop open sockets.** Existing WebSockets continue
  until they next reconnect, and only then does the new key apply.
- **Stale WebSocket clients are discarded.** The library's automatic reconnect
  would resend a spent nonce, so the poller builds a new client for each attempt.
- **Refused config pushes back off.** The controller sends one config push to a
  member at a time. After a 401 or 403 it waits 30 seconds before trying again,
  and 5 seconds after any other failure, so a member with the wrong key is not
  sent a nonce request and a POST on every 500 ms tick.
- **Upgrade order.** A peer on older firmware has no nonce endpoint. Requests to
  it go unsigned, which works only if it has no password.

## Testing

- `test/test_loadsharing_auth/`: signing, parsing, scope, field allowlist, nonce
  lifetime, rollover and eviction. One case pins the signature to a value computed
  independently with Python.
- `tests/integration/test_loadsharing_peer_auth.py`: wire format, replay, scope,
  and a real two-charger group with passwords set, the regression the issue asked
  to catch in CI.
