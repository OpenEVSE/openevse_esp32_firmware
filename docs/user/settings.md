# Settings reference

The Settings hub groups every configuration page into four sections; pages
that need particular hardware (e.g. the TFT display) only appear when that
hardware is present.

![Settings hub](screenshots/settings-dark-desktop.png)

## Connectivity

- **[Network](screenshots/settings-network-dark-desktop.png)** — WiFi
  scan/join, access-point fallback, hostname, wired Ethernet on supported
  boards. See [Getting started](getting-started.md).
- **[HTTP](screenshots/settings-http-dark-desktop.png)** — web access:
  admin username/password (strongly recommended — HTTP auth then protects
  every request including the input APIs), TLS certificate selection, and UI
  language. Four languages ship: English, Spanish, French, Hungarian.
- **[MQTT](screenshots/settings-mqtt-dark-desktop.png)** — broker, credentials,
  base topic. See [Integrations](integrations.md).
- **[OCPP](screenshots/settings-ocpp-dark-desktop.png)** — see [OCPP](ocpp.md).

## Charger

- **[Charger](screenshots/settings-evse-dark-desktop.png)** — service level,
  maximum current, LCD/LED options, button behaviour, three-phase mode.
- **[Safety](screenshots/settings-safety-dark-desktop.png)** — see
  [Safety](safety.md).
- **Time & Date** <a id="time--date"></a>
  ([screenshot](screenshots/settings-time-dark-desktop.png)) — SNTP time sync,
  timezone (POSIX rules, DST handled automatically), manual sync. Schedules
  depend on this being right.
- **[RFID](screenshots/settings-rfid-dark-desktop.png)** — see [RFID](rfid.md).
- **[Vehicle](screenshots/settings-vehicle-dark-desktop.png)** — see
  [Vehicle](vehicle.md).

## Energy

- **[Self-production](screenshots/settings-solar-dark-desktop.png)** — see
  [Solar divert](solar-divert.md).
- **[Load Shaper](screenshots/settings-shaper-dark-desktop.png)** — see
  [Load shaper](load-shaper.md).
- **[EmonCMS](screenshots/settings-emoncms-dark-desktop.png)** and
  **[OhmConnect](screenshots/settings-ohmconnect-dark-desktop.png)** — see
  [Integrations](integrations.md).

## System

- **[Firmware](screenshots/settings-firmware-dark-desktop.png)** — see
  [Firmware update](firmware-update.md).
- **[Certificates](screenshots/settings-certificates-dark-desktop.png)** —
  upload/manage TLS certificates used for MQTTS and HTTPS connections to your
  broker and servers.
- **[Developer Tools](screenshots/settings-terminal-dark-desktop.png)** — a
  live RAPI terminal to the controller and the gateway debug console. RAPI
  command reference: [rapi.md](../rapi.md).
- **[Display](screenshots/settings-display-dark-desktop.png)** — theme,
  brightness, and sleep timeout for the on-device TFT touchscreen (only shown
  on TFT-equipped hardware).
- **[About](screenshots/settings-about-dark-desktop.png)** — versions, device
  info, and diagnostics.
