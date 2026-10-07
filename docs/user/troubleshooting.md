# Troubleshooting & reset

## Why isn't it charging?

1. Check the [Dashboard](dashboard.md) — is the mode **Off**? Is a
   [scheduled rule](charge-manager.md) or [session limit](dashboard.md#session-limits)
   active? Is [Eco mode](solar-divert.md) waiting for solar excess?
2. Check [Monitoring → Manager](monitoring.md): the claims list shows exactly
   which subsystem is holding the charger in its current state and why.
3. A red ring is a hardware fault (GFCI, no ground, stuck relay, over
   temperature) — see [Safety](safety.md) and the counters on
   Monitoring → Safety.

## WiFi reset (keep other settings)

- Hold the external button ~10 s until the unit enters access-point mode,
  then reconfigure WiFi as in [Getting started](getting-started.md).
- Holding the module's `boot/GPIO0` button ~5 s also forces AP mode without
  erasing anything.

## HTTP password reset

Hold the external button ~10 s → connect to the AP → choose **WiFi
Standalone** → set new HTTP auth credentials.

## Factory reset (all configuration lost)

- From the web UI, or
- Press and hold the `GPIO0` button on the WiFi module for ~10 s.

## Firmware recovery

If the unit reboot-loops after an update, erase the flash completely and
re-flash over USB (see [Firmware update](firmware-update.md)):

```bash
esptool.py erase_flash
```

## Reporting an unexpected reboot

If the unit rebooted or reboot-looped on its own, it stores a crash report in
flash that survives the reboot. Read it over the network -- no serial cable
needed:

```bash
curl -u openevse:<password> http://<charger>/debug/crash
```

The response names the panic reason, the task that faulted and a backtrace.
Paste it into your GitHub issue along with the firmware version; it is the
single most useful thing you can attach. `curl -X DELETE` on the same URL
clears the stored report, so the next one is unambiguously new.

Developers chasing a crash can fetch the raw dump from
`/debug/crash/raw` and decode it with `esp-coredump` against the exact
`firmware.elf` the unit is running -- the `elf_sha256` field says which build
that is.

### Sending a crash report to OpenEVSE

On 16 MB boards, **Developer Tools** has a **Send to OpenEVSE** button beside the
stored report. Your browser sends the decoded report to `crash.openevse.com`,
where the maintainers see it as a readable stack trace -- no need to find a
matching `firmware.elf` or attach anything to an issue. The browser does the
sending, so it is the browser that needs internet access, not the charger.

Before you press it:

- **Only the decoded summary is sent, never the raw dump.** That is the panic
  reason, the crashed task, the program counter and the backtrace, plus the
  build, the chip model, heap figures, and which features are switched on. Your
  Wi-Fi, MQTT and other passwords, hostnames and addresses are not included:
  the charger builds the report from an allowlist of settings. The raw dump is a
  copy of the charger's memory and can hold those, so it stays on the charger
  (fetch it yourself from `/debug/crash/raw` if a maintainer asks). The report is
  kept privately for 90 days, and is then deleted.
- **Nothing in it names the charger.** Instead of the chip id (which is derived
  from the network MAC address), the first report creates a random reporter id,
  stored on the charger and shown under Developer Tools.
- **You can erase everything this charger has sent.** **Delete my reports from
  OpenEVSE** removes every report the charger sent and forgets the reporter id,
  so a later report is not linked to the old ones. It works with a secret delete
  key stored on the charger alongside the reporter id and never sent with a
  report, so nobody else can delete your reports.
- **The report is removed from the charger only once it has arrived.** If the
  browser cannot reach `crash.openevse.com` (for example a phone connected to
  the charger's own hotspot), nothing is lost: try again from a browser that is
  online.

The page uses these endpoints; with a password set, add `-u openevseadmin:<password>`
to each -- `-u <user>:<password>` if you set a user name. The identity routes
answer only requests carrying `X-Requested-With: OpenEVSE`, as the page's do,
so another web page cannot change the identity behind your back:

```sh
H='X-Requested-With: OpenEVSE'
curl -H "$H" http://<charger>/debug/crash/identity          # reporter id, broker URL
curl -H "$H" 'http://<charger>/debug/crash/identity?key=1'  # ... and the delete key
curl http://<charger>/debug/crash/report                    # the report, exactly as sent
curl -H "$H" -X DELETE http://<charger>/debug/crash/identity  # forget the identity
```

`/debug/crash/report` answers `404` with no dump stored, and `409` until a
reporter identity has been set (`POST /debug/crash/identity` with
`{"reporter_id": <32 hex>, "delete_key": <64 hex>}`; the page does this for you).

## Sharing a charging problem as a replay

When the charger does something unexpected -- a schedule that did not start,
solar divert that stopped too early, a session limit that cut out, a feature
fighting another -- download a **replay package** right after it happens and
attach it to your GitHub issue:

```bash
curl -u openevse:<password> -o replay.json 'http://<charger>/debug/replay?download=1'
```

(or open `http://<charger>/debug/replay?download=1` in a browser). The charger
keeps the **last hour**: every 10 seconds the solar, grid and site power, the
voltage, the EVSE temperature, whether a car is plugged in, the charger state,
the pilot and the current drawn; and every change of the claims that decide
what the charger does (schedule, solar divert, grid shaping, RFID, OCPP,
manual override, ...), session limit, boost, RFID authorisation, schedule and
configuration. The recording restarts when the charger reboots.

Maintainers replay it through the firmware's own logic in the simulator
(`divert_sim/replay.py`), which shows where the firmware's decisions differ
from what was expected.

What is and is not in it:

- **Included:** the firmware version, the time zone, the Charge Manager
  schedule, and the settings that change charging behaviour (station defaults,
  session limit, solar divert, grid shaping, temperature protection, which
  features are on).
- **Not included:** Wi-Fi, MQTT, OCPP, emoncms and web passwords and keys,
  hostnames, server addresses and MQTT topics -- the package is built from an
  allowlist of settings, so anything not on it stays on the charger. Stored
  RFID card numbers are replaced by placeholders.

## Getting help

- [OpenEVSE knowledge base & support](https://openevse.dozuki.com/)
- [GitHub issues](https://github.com/OpenEVSE/openevse_esp32_firmware/issues)
  for firmware bugs — one issue per problem, with your firmware version
  (Settings → About) and steps to reproduce.
