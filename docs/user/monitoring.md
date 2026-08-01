# Monitoring

Live metrics from the charger, grouped into four tabs.

![Monitoring screen](screenshots/monitoring-dark-desktop.png)

- **Data** — energy delivered (session / today / week / month / year / total),
  sensors (pilot, current, voltage, EVSE temperature, individual temperature
  sensors), vehicle data (battery %, range, time to full), and home battery
  state where configured.
- **Energy** — charts of logged energy over time (see [History](history.md)
  for the underlying log).
- **Safety** — the protection counters: GFCI trips, no-ground events, stuck
  relay detections. Rising counters are worth investigating — see
  [Safety](safety.md).
- **Manager** — the claims currently registered on the charger (which
  subsystem is controlling state and current, at what priority) and service
  connection status. Invaluable for answering "*why* is it (not) charging?"

## Temperature sensors

Depending on hardware, up to four sensors are reported: the RTC sensor (older
LCD modules), the MCP9808 (newer LCD modules), an IR sensor (unused), and the
OpenEVSE WiFi V1 module's own sensor. The highest reading drives
[temperature throttling](safety.md#temperature-throttling).
