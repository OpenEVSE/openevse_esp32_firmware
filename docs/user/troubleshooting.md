# Troubleshooting & reset

## Why isn't it charging?

1. Check the [Dashboard](dashboard.md) — is the mode **Off**? Is a
   [schedule](schedule.md) or [session limit](dashboard.md#session-limits)
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

## Getting help

- [OpenEVSE knowledge base & support](https://openevse.dozuki.com/)
- [GitHub issues](https://github.com/OpenEVSE/openevse_esp32_firmware/issues)
  for firmware bugs — one issue per problem, with your firmware version
  (Settings → About) and steps to reproduce.
