# TFT display controls — GUI handoff (nightshift UI agent)

The firmware exposes three new config keys (LVGL-TFT builds only). Read/write them
via the same `/config` GET/POST the theme control already uses. **Presence of the
keys in `GET /config` is the capability signal** — show the controls only when present
(same contract as `tft_theme`).

| Key | Type | Range | Default | Control |
|---|---|---|---|---|
| `tft_brightness` | int | 10–100 | 100 | Active brightness slider (%) |
| `tft_standby_brightness` | int | 0–100 | 15 | Standby brightness slider (%); 0 = screen off |
| `lcd_backlight_timeout` | int (seconds) | 0, or 5–3600 | 600 | Idle timeout; 0 = "Never sleep" (slider + Never, or presets) |

> Note: the timeout key is `lcd_backlight_timeout` (shared with the char-LCD / TFT_eSPI
> energy-saving timeout from upstream PR #1039), **not** a `tft_`-prefixed key. The two
> brightness keys remain LVGL-specific (`tft_`-prefixed).

POST example: `POST /config {"tft_brightness":60,"tft_standby_brightness":10,"lcd_backlight_timeout":300}`.

Place these in the existing **Display** section alongside the theme selector. Changes
apply live on the device (no reboot).

## Behaviour notes (for tooltips / help text)

- **Active brightness** clamps to a 10% floor in firmware — a value below 10 is treated
  as 10 so the active screen can never go fully dark.
- **Standby brightness = 0** turns the backlight fully off on timeout (no standby screen
  drawn) — i.e. the classic blank-on-idle behaviour.
- **Idle timeout = 0** keeps the screen at active brightness indefinitely.
- The display stays at full brightness while **charging or in a fault state** regardless
  of the timeout; standby only engages when idle.
