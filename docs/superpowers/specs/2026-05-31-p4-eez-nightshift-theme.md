# P4 EEZ Studio Theme — Nightshift palette

Companion to `2026-05-30-esp32-p4-d4-eez-ui-integration-brief.md`. This makes the
on-device LVGL screen (P4, 800×480 landscape) visually match the **nightshift web
GUI**. Colors are extracted verbatim from the nightshift source of truth:
`gui-nightshift/src/app.css` (`:root`/`[data-theme="light"]` and `[data-theme="dark"]`).
chartTheme.js only re-references these same tokens, so app.css is authoritative.

## 1. Define the colors (EEZ Studio → Settings → Colors)

Create these **named colors**. Names become C identifiers on LVGL export, so no
hyphens. Add two **Themes**: `Light` and `Nightshift`. Nightshift is the primary
on-device theme; Light is optional (kept for parity with the web GUI's light mode).

| Color name | Light theme | Nightshift theme (dark) |
|---|---|---|
| `surface`   | `#FFFFFF` | `#0C0E13` |
| `surface2`  | `#EEF4F3` | `#10141C` |
| `surface3`  | `#DDE7E6` | `#161B26` |
| `text`      | `#13202B` | `#E8ECF2` |
| `textDim`   | `#5B6B72` | `#6B7585` |
| `accent`    | `#0F9B98` | `#3CC6BD` |
| `border`    | `#E4EAE9` | `#1C2230` |
| `charging`  | `#0F9B98` | `#3CC6BD` |
| `error`     | `#D6453D` | `#F06E66` |
| `warning`   | `#D98A2B` | `#E7A948` |
| `sleep`     | `#6792B3` | `#7DA7C8` |
| `success`   | `#2EA052` | `#5DC975` |

Set the active theme to **Nightshift**. In each style attribute, drag the named
color onto the attribute (or type the color name) rather than hardcoding hex, so
the theme switch re-skins everything.

Note: the web GUI's dark theme adds an `--accent-glow`
(`0 0 8px rgba(60,198,189,0.55)`) used as a CSS box-shadow on the PowerRing. LVGL
has no direct equivalent; approximate with a `Shadow` style (color `accent`, spread
~8px, low opacity) on `charge_power_ring` if you want the glow.

## 2. Apply to the integration-brief widgets

Per-screen color usage (widget names from §3 of the integration brief):

### `screen_charge`
| Widget | Background | Text / fill |
|---|---|---|
| screen base | `surface` | — |
| `charge_state_label` | — | `text` (or `charging` when active) |
| `charge_kw_value` | — | `text` (big) |
| `charge_amps_value` / `charge_volts_value` / `charge_energy_value` / `charge_elapsed_value` | — | `textDim` for units, `text` for value |
| `charge_temp_value` | — | `textDim` (→ `warning` if hot) |
| `charge_power_ring` (Arc) | track = `border` | indicator = `charging` (active) / `sleep` (paused) |
| `charge_wifi_label` | — | `textDim` |
| `btn_start_stop` | `surface3` (idle) / `accent` (actionable) | label `text` |
| panels / cards | `surface2` or `surface3` | — |
| dividers | `border` | — |

### `screen_boot`
| Widget | Color |
|---|---|
| screen base | `surface` |
| `boot_status_label` | `textDim` |
| `boot_progress` | fill `accent`, track `border` |

### `screen_sleeping`
| Widget | Color |
|---|---|
| screen base | `surface` |
| `sleeping_state_label` | `sleep` |
| `btn_wake` | bg `surface3`, label `text` |

### `screen_fault`
| Widget | Color |
|---|---|
| screen base | `surface` |
| `fault_text_label` | `error` |

## 3. Optional: merge straight into the `.eez-project` JSON

If preferred over hand-entering, the `.eez-project` file is JSON with `colors` and
`themes` arrays. The exact schema is EEZ-version-specific, so share the project
file (or its version) and the values above can be emitted as a ready-to-merge JSON
snippet rather than re-typed in the UI.
