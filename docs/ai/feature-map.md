# Feature map

Every user-facing feature, mapped to its firmware sources, web UI route, config
options, and APIs. This is the coverage backbone for the documentation: each
row must have a user-doc page, and new features add a row here.

Conventions: firmware paths are under `src/`; UI routes are gui-nightshift hash
routes (`#/…`); config prefixes refer to option names in `app_config.cpp`;
HTTP paths are device endpoints per [api.yml](../../api.yml); MQTT topics are
relative to the configured base topic (see [mqtt.md](../mqtt.md)).

| Feature | Firmware source | UI route | Config options | HTTP API | MQTT | User doc |
|---|---|---|---|---|---|---|
| Charge state & mode (Auto/Eco/On/Off), manual override | `evse_man.*`, `manual.*` | `/` | `default_state`, `pause_uses_disabled` | `/status`, `/override`, `/claims` | `override/set`, `status` | [dashboard.md](../user/dashboard.md) |
| Charge rate / current control | `evse_man.*`, `evse_monitor.*` | `/` | `max_current_soft` | `/config`, `/override` | `charge_rate/set` | [dashboard.md](../user/dashboard.md) |
| Session limits (energy/time/SOC/range) | `limit.*` | `/` (limit pills) | `limit_default_type`, `limit_default_value` | `/limit` | `limit/set` | [dashboard.md](../user/dashboard.md) |
| First-run setup wizard | — (UI only) | `/` until passed | `wizard_passed` | `/config` | — | [getting-started.md](../user/getting-started.md) |
| Weekly charge schedule | `scheduler.*` | `/schedule` | `scheduler_start_window` | `/schedule`, `/schedule/plan` | `schedule/set` | [schedule.md](../user/schedule.md) |
| Solar divert / Eco mode | `divert.*` | `/settings/solar` | `divert_*` (`divert_enabled`, `divert_type`, `divert_PV_ratio`, smoothing/attack/decay, min charge time) | `/config`, `/status` (`solar`, `grid_ie`) | `divertmode/set`, solar/grid topics | [solar-divert.md](../user/solar-divert.md) |
| Current shaper (grid power cap) | `current_shaper.*` | `/settings/shaper` | `current_shaper_*` | `/config` | live power topic | [load-shaper.md](../user/load-shaper.md) |
| Temperature throttling | `temp_throttle.*` | `/settings/safety` | `temp_throttle_*`, `over_temp_shutdown` | `/config` | — | [safety.md](../user/safety.md) |
| Safety checks (diode/GFCI/ground/relay/vent), boot lock, heartbeat | `evse_man.*`, controller | `/settings/safety` | `*_check` flags, `boot_lock`, `heartbeat_*` | `/config` | — | [safety.md](../user/safety.md) |
| Energy metering (session/day/week/month/year) | `energy_meter.*` | `/monitoring`, `/history` | — | `/emeter`, `/status` | `session_energy`, `total_energy`, … | [monitoring.md](../user/monitoring.md) |
| Energy time-series logging | `energy_logger.*` | `/history` | — | `/logs`, energy endpoints | — | [history.md](../user/history.md) |
| Event log | `event_log.*` | `/history` | — | `/logs` | — | [history.md](../user/history.md) |
| MQTT integration (incl. Home Assistant) | `mqtt.*` | `/settings/mqtt` | `mqtt_*` | `/config`, `/status` | everything | [integrations.md](../user/integrations.md) |
| EmonCMS logging | `emoncms.*` | `/settings/emoncms` | `emoncms_*` | `/config`, `/status` | — | [integrations.md](../user/integrations.md) |
| OCPP 1.6 | `ocpp.*` | `/settings/ocpp` | `ocpp_*` | `/config` | — | [ocpp.md](../user/ocpp.md) |
| RFID authentication | `rfid.*` | `/settings/rfid` | `rfid_enabled`, `rfid_storage` | `/config`, RFID endpoints | `rfid/…` | [rfid.md](../user/rfid.md) |
| Vehicle SOC/range (Tesla / MQTT / OCPP sources) | `tesla_client.*`, `vehicle.*` | `/settings/vehicle` | `tesla_*`, `mqtt_vehicle_*`, `vehicle_data_src` | `/tesla/vehicles`, `/config` | vehicle topics | [vehicle.md](../user/vehicle.md) |
| Ohm Connect demand response | `ohm.*` | `/settings/ohmconnect` | `ohm`, `ohm_enabled` | `/config` | — | [integrations.md](../user/integrations.md) |
| WiFi / wired Ethernet / AP mode | `net_manager.*` | `/settings/network` | `ssid`, `pass`, `ap_*`, `hostname` | `/config`, `/scan`, `/status` | — | [getting-started.md](../user/getting-started.md) |
| HTTP auth & web access | `web_server.*` | `/settings/http` | `www_*` | `/config` | — | [settings.md](../user/settings.md) |
| Time & timezone (SNTP) | `time_man.*` | `/settings/time` | `sntp_*`, `time_zone` | `/time`, `/config` | — | [settings.md](../user/settings.md) |
| Firmware update (web upload / GitHub OTA) | `web_server.*`, `ota.*` | `/settings/firmware` | — | `/update`, `/restart` | — | [firmware-update.md](../user/firmware-update.md) |
| SSL certificates | `certificates.*` | `/settings/certificates` | `*_certificate_id` | `/certificates` | — | [settings.md](../user/settings.md) |
| RAPI terminal / debug console | `web_server.*` | `/settings/terminal` | — | `/r`, WS consoles | — | [settings.md](../user/settings.md) |
| On-device TFT display | `lcd_tft.*`, `src/lvgl_tft/` | `/settings/display` (gated on `tft_theme`) | `tft_*` | `/config` | — | [settings.md](../user/settings.md) |
| Charger info / diagnostics | `evse_monitor.*` | `/monitoring`, `/settings/about` | — | `/status`, `/config` | telemetry topics | [monitoring.md](../user/monitoring.md) |

## Maintenance

- Adding a feature? Add a row **in the same PR**, plus the user-doc page it
  points to.
- The screenshot manifest (`gui-nightshift/scripts/screenshots.config.js`)
  should have a capture for every UI route listed here.
- `scripts/docs_coverage.py` (see CI) cross-checks config options and UI routes
  against the documentation and this map.
