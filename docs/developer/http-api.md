# HTTP API index

The full reference, with request and response schemas, is [api.yml](../../api.yml)
(also published on
[Stoplight](https://openevse.stoplight.io/docs/openevse-wifi-v4/)). This page is
the route index: every route registered in `src/web_server*.cpp` has an entry in
`api.yml`, and `scripts/check_api_routes.py` (run by the *Validate API docs*
workflow) fails the build if one is added without it.

## Authentication

Authentication is only enforced when an admin password is set. It then applies to
every route except `/login`, `/logout` and `/emoncms/describe`: send HTTP Basic
credentials, or the `oevse_session` cookie from `POST /login`. Failures are `401`
(JSON `{"msg":"auth"}` when `X-Requested-With: OpenEVSE` is sent, otherwise a
`WWW-Authenticate` challenge), `429` (wrong credentials while the brute-force
throttle is active) and `403` (CSRF guard). The details are in the
*Authentication* section of `api.yml`; the implementation is
`requestPreProcess()` in `src/web_server.cpp`.

## Routes

| Area | Routes |
|---|---|
| Authentication | `/login`, `/logout` |
| State and configuration | `/status`, `/config`, `/override`, `/claims`, `/claims/target`, `/limit`, `/boost`, `/schedule`, `/schedule/plan`, `/divertmode`, `/shaper` |
| Time | `/time`, `/settime` (legacy alias of `POST /time`) |
| Logs and energy | `/logs`, `/logs/export`, `/energy/raw`, `/energy/daily`, `/energy/weekly`, `/energy/monthly`, `/energy/annual`, `/emeter` |
| RFID | `/rfid/users`, `/rfid/add` |
| Notifications | `/notifications`, `/notifications/ack` |
| Load sharing | `/loadsharing/peers`, `/loadsharing/peers/{host}`, `/loadsharing/discover`, `/loadsharing/status` |
| Relay and cable | `/relay/reset`, `/relay/recovery`, `/cabletemp` |
| Integrations | `/mqtt`, `/tesla/vehicles`, `/teslaveh` (legacy alias), `/emoncms/describe` |
| Network and system | `/scan`, `/apoff`, `/restart`, `/reset`, `/certificates` |
| Firmware | `/update`, `/migrate/status`, `/migrate/coredump`, `/migrate/expand16mb` |
| Diagnostics | `/debug`, `/debug/console`, `/debug/crash`, `/debug/crash/raw`, `/evse`, `/evse/console`, `/rapi` (`/r` is an alias) |
| Events | `/ws` |

Operations that change state (`/reset`, `/restart`, `/apoff`, `/divertmode`,
`/shaper`, `/rfid/add`, `/rapi` with a command, and the relay operations) reject
a `GET` that does not carry `X-Requested-With: OpenEVSE`, so a cross-site link
cannot trigger them. Use `POST`, or send the header.
