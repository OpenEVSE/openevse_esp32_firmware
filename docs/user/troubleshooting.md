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

## Getting help

- [OpenEVSE knowledge base & support](https://openevse.dozuki.com/)
- [GitHub issues](https://github.com/OpenEVSE/openevse_esp32_firmware/issues)
  for firmware bugs — one issue per problem, with your firmware version
  (Settings → About) and steps to reproduce.
