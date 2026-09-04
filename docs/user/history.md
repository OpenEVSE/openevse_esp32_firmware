# History

The charge and event log — every session, state change, and warning, with
energy delivered, temperature, and the RFID user where applicable.

![History screen](screenshots/history-dark-desktop.png)

- **Charging** entries record the energy delivered in that session.
- **Warnings** (e.g. *Stuck Relay*) are highlighted — repeated warnings are a
  hardware signal, see [Troubleshooting](troubleshooting.md).
- **Export CSV** downloads the log for spreadsheets or record-keeping.

## How energy data is stored

The device keeps its energy log on internal flash, aggregated in tiers so it
never outgrows the available space: a rolling 6-hour raw buffer (60-second
samples), daily metrics for 6 months, monthly metrics per year, and annual
totals. The [Monitoring → Energy](monitoring.md) charts read from this log.

For long-term, full-resolution logging, feed the data to an external system —
see [EmonCMS and MQTT integrations](integrations.md).
