# Home Assistant OAuth Connection — Design

**Date:** 2026-06-01
**Status:** Approved (design); implementation plan to follow.

## Goal

Add a **global Home Assistant connection** to the OpenEVSE ESP32 firmware that
authenticates via Home Assistant's OAuth2 (IndieAuth) **browser-redirect flow**, with
the ESP32 acting as its own `client_id` and `redirect_uri` on the LAN. v1 establishes
and maintains the authenticated connection and surfaces it as a "Connect to Home
Assistant" control on the main settings page. Reading entity data and wiring values
into existing features (divert grid power, vehicle SoC, a generic entity reader) is
explicitly **deferred** to follow-on work — but v1 leaves a clean seam for it.

User chose OAuth over a long-lived access token, and an on-device automatic callback
(ESP32 captures `?code=` on `/ha_callback`) over manual code-paste.

## Scope

**In scope (v1):**
- OAuth browser-redirect login, end to end, initiated from the main settings page.
- On-device authorization-code → token exchange and automatic refresh.
- Token persistence across reboots; connection status; disconnect.
- A reusable, bearer-attaching firmware seam (`homeAssistant.get(...)`) — present but
  with no callers yet.
- gui-nightshift settings block: HA URL, Connect/Disconnect, status.

**Out of scope (deferred):**
- Reading/polling entity states.
- Wiring HA values into divert / current-shaper / EVSE-manager SoC.
- A generic configurable entity reader.
- mDNS auto-discovery of the HA instance.
- PKCE (only if HA's auth endpoint is confirmed to support it during implementation;
  otherwise omitted — see Security).

## Reference

evcc's HA integration was the study reference (`util/homeassistant/{oauth2,connection,
instance,zeroconf}.go`). It uses the same OAuth2 endpoints (`/auth/authorize`,
`/auth/token`, `AuthStyleInParams`) and attaches `Authorization: Bearer` to plain REST
calls. evcc is a server with a configured external URL; we instead derive the device
URL from the request `Host` header (see Data Flow). HA auth API:
https://developers.home-assistant.io/docs/auth_api

The Tesla client (`src/tesla_client.cpp`, `tesla_*` config in `app_config.cpp`) is the
in-repo precedent for token storage (`ConfigOptSecret`), the `CONFIG_SERVICE_*` enable
flag, `config_changed()` dispatch, and the async `MongooseHttpClient` request pattern.
Note Tesla does its OAuth **off-device**; HA differs by doing it on-device.

## Architecture & Components

Three clean seams: the firmware module knows nothing about the UI or any consumer; the
UI knows only the REST endpoints; future features know only `homeAssistant.get(...)`.

### Firmware (this repo)

- **`src/home_assistant.{h,cpp}`** — new `HomeAssistantClient`, a `MicroTasks::Task`
  with a global instance `homeAssistant`, structured like `mqtt` / `tesla_client`.
  Owns the entire token lifecycle. Public surface (kept small):
  - `begin()`, `loop(WakeReason)` — refresh scheduling.
  - `isConnected()` — enabled && have a non-expired or refreshable access token.
  - `getStatus(JsonDocument&)` — fills `{enabled, connected, ha_url, expires}`.
  - `beginAuthorize(const String &redirectUri, String &outAuthUrl)` — generates and
    stashes `state`, builds the HA authorize URL. Used by the web handler.
  - `handleCallback(const String &code, const String &state)` — validates `state`,
    performs the token exchange, stores tokens. Used by the web handler.
  - `disconnect()` — clears tokens, cancels refresh.
  - `get(const String &path, responseCallback)` — reusable helper that attaches
    `Authorization: Bearer <token>`. **Present in v1, no callers yet.**
- **`src/web_server_home_assistant.cpp`** — HTTP handlers, registered with
  `server.on(...)` in `web_server_setup()` (alongside the existing `/tesla/...` lines).
- **`src/app_config.cpp` / `app_config.h`** — persisted config + a new
  `CONFIG_SERVICE_HOMEASSISTANT` service-flag bit; `config_changed()` gains a
  `name.startsWith("ha_")` / `home_assistant_enabled` branch that pushes values into
  `homeAssistant`, mirroring the `tesla_` branch.
- **`src/main.cpp`** — construct/`begin()` the task and pump it like the other
  integration tasks.

### UI (`gui-nightshift` repo — separate git repo)

- **`src/lib/config/homeassistant.js`** — HA field defs + connect-flow trigger.
- **Home Assistant block on the main settings page** (conventions from
  `src/routes/settings/Mqtt.svelte`, `src/lib/config/tesla.js`): HA URL field,
  Connect/Disconnect buttons, status indicator.
- vitest + testing-library/svelte tests alongside (like `tesla.test.js`,
  `Mqtt.test.js`); uses the `dev:mock` harness.

## Data Flow (OAuth)

```
Browser (on LAN)            ESP32 firmware                 Home Assistant
─────────────────────────────────────────────────────────────────────────
1. user clicks "Connect" ─► GET /ha/auth/start
                            • from request (scheme + Host) build
                              client_id  = <scheme>://<host>/
                              redirect_uri = <scheme>://<host>/ha_callback
                            • gen random `state` (esp_random), stash
                              {state, redirect_uri} with short TTL
                            • 302 ───────────────────────────────────────►
2.                                                         /auth/authorize?
                                                           client_id&redirect_uri&state
                          ◄──────────── user logs in, approves ───────────
3. browser redirected ──► GET /ha_callback?code=…&state=…
                            • verify `state` matches the stashed value
                            • POST /auth/token ───────────────────────────►
                              grant_type=authorization_code, code,
                              client_id, redirect_uri (replayed from stash)
                          ◄──── { access_token, refresh_token, expires_in }
                            • store tokens (ConfigOptSecret) + expiry
                            • 302 → settings page (?ha=connected | ?ha=error=…)
4. (ongoing) loop() refresh:    POST /auth/token grant_type=refresh_token ►
                          ◄──── { access_token, refresh_token, expires_in }
```

- `client_id` / `redirect_uri` are **derived from the incoming request** at
  `/ha/auth/start` — the `Host` header gives host:port (so it matches whatever the
  browser used, `.local` name or raw IP, satisfying HA's same-host rule with zero
  config) and the **scheme follows how the browser reached the EVSE** (`http`, or
  `https` if the EVSE is served over TLS per #20) — this is the *device's* scheme and
  is independent of `ha_url`'s scheme. The **same** `redirect_uri` is replayed at token
  exchange (OAuth requires it to match).
- `code`→token exchange and all refreshes are **server-to-server from the ESP32**
  (`MongooseHttpClient`); secret tokens never enter the browser.
- `state` is single-use CSRF protection.

## Token Lifecycle & Config

Persisted config (in `app_config.cpp`, following the Tesla block):

| Field | Type | Notes |
|---|---|---|
| `ha_url` | `ConfigOptDefinition<String>` | e.g. `http://homeassistant.local:8123`; scheme (http/https) honored |
| `ha_access_token` | `ConfigOptSecret` | masked in `/config` output, like `tesla_access_token` |
| `ha_refresh_token` | `ConfigOptSecret` | masked |
| `ha_token_expires` | `ConfigOptDefinition<uint64_t>` | absolute unix expiry = created + expires_in |
| `home_assistant_enabled` | `ConfigOptVirtualMaskedBool` on `CONFIG_SERVICE_HOMEASSISTANT` | new service-flag bit |

- **Refresh scheduling**: `loop()` returns a wake interval; when connected it refreshes
  **~5 minutes before** `ha_token_expires` via `POST /auth/token`
  (`grant_type=refresh_token`). On success it rewrites the access/refresh tokens (HA
  rotates the refresh token) and `ha_token_expires`, saves config, and emits a
  `home_assistant` event.
- **Reboot persistence**: the refresh token is long-lived. On `begin()`, if a refresh
  token exists and the service is enabled, the module is connectable and refreshes on
  first loop if the access token is stale.

## Web Endpoints

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/ha/auth/start` | basic-auth (requestPreProcess) | Build authorize URL, 302 to HA |
| GET | `/ha_callback` | `state` only (no basic-auth) | Validate state, exchange code, store tokens, 302 to settings |
| GET | `/ha/status` | basic-auth | JSON `{enabled, connected, ha_url, expires}` |
| POST | `/ha/disconnect` | basic-auth | Clear tokens, cancel refresh |

`/ha_callback` cannot require basic-auth because HA's browser redirect won't carry it;
it is protected by the unguessable single-use `state` + immediate exchange instead.
`/ha/status` may alternatively be folded into the existing `/status` payload the
dashboard already polls; decided at implementation time.

## UI (gui-nightshift)

- Connect is a **full browser navigation** to `/ha/auth/start` (not a `fetch`), so HA's
  login page and the redirect chain run in the real browser. On return the firmware
  redirects to the settings page with `?ha=connected` or `?ha=error=…`, which the page
  reads to show a toast.
- HA URL field saved via the normal `/config` store; Connect enabled once a URL is
  saved.
- Status indicator (Connected / Not connected) driven by `/ha/status` (or the folded
  `/status` fields). Disconnect button → `POST /ha/disconnect`, then refresh status.
- Strings via `svelte-i18n`. The block reads/writes only via documented endpoints.

## Error Handling & Security

- **CSRF**: `state` from `esp_random()` at `/ha/auth/start`, stored with a short TTL
  (single pending auth), required to match at `/ha_callback`; mismatch/expired → 400,
  no exchange.
- **redirect_uri integrity**: the token-exchange `redirect_uri` is the value stashed
  with `state` at start, not re-derived from the callback request, so a tampered
  callback Host can't shift it.
- **Endpoint auth**: `/ha/auth/start`, `/ha/disconnect`, `/ha/status` go through the
  existing `requestPreProcess` HTTP-auth gate. `/ha_callback` is protected by `state`
  (see Web Endpoints).
- **Token exchange failure** (non-200 from `/auth/token`): clear pending state, emit a
  `home_assistant` error event, leave any existing tokens untouched (a failed *re*-auth
  must not nuke a working connection).
- **Refresh failure**: transient (network/5xx) → retry with backoff, stay connected
  until expiry actually lapses; hard failure (`400 invalid_grant` = refresh token
  revoked in HA) → clear tokens, flip to disconnected, surface in status.
- **Secrets**: tokens stored as `ConfigOptSecret` (masked in `/config`); server-to-
  server exchange keeps them out of the browser; if `ha_url` is `https`, exchange and
  refresh use the existing Mongoose TLS stack (relates to tasks #10/#20).
- **Disable/clear**: toggling `home_assistant_enabled` off, or Disconnect, clears
  tokens and cancels refresh scheduling.
- **PKCE**: verify during implementation whether HA's authorize endpoint supports it.
  If yes, add `code_challenge`/`code_verifier` (S256 via mbedtls). If not, rely on
  `state` + single-use code + immediate server-side exchange. Not promised in v1.

## Testing

- **Firmware host-side unit tests** (pure functions, no network), under `test/` per the
  repo's native test setup:
  - authorize-URL builder: (host, ha_url, state) → correct `/auth/authorize?…`.
  - `client_id` / `redirect_uri` derivation from a request (scheme + `Host` header).
  - `state` generate/verify (including mismatch/expiry).
  - token-response parser: JSON → {access, refresh, expiry}.
  - expiry / refresh-due calculator.
- **Firmware integration (manual, on hardware)**: full click-through against a real HA
  instance — Connect → login → callback → `isConnected()` true; reboot persists;
  Disconnect clears; revoke-in-HA → next refresh flips to disconnected. Verified via
  `/ha/status` + the UI (serial console is not readable from the build host).
- **gui-nightshift**: vitest + testing-library/svelte for the settings block — renders
  fields, Connect disabled until URL saved, status reflects `/ha/status`, Disconnect
  posts — mirroring `tesla.test.js` / `Mqtt.test.js`; uses `dev:mock`.

## Open Questions / Decided-at-Implementation

- Whether `/ha/status` is a standalone endpoint or folded into the existing `/status`
  payload.
- Whether HA supports PKCE (drives the optional PKCE addition).
- Exact `state` TTL and storage (single pending-auth slot vs small map).
