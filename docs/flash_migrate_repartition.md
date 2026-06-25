# In-place 16MB flash repartition (`flash_migrate`) — design & handoff

Migrates a 16MB ESP32 module that was flashed with the **4MB** partition layout
(`min_spiffs.csv`) to the full **16MB** layout (`openevse_16mb.csv`)
**over-the-air**, from Settings → Developer Tools → **Flash & storage → Expand
partition to 16MB**. **Hardware-validated end to end** (16MB ESP32-D0WD-V3): 4MB
OpenEVSE → full 16MB OpenEVSE in ~75s, fully OTA, no USB, WiFi creds preserved.

## Why an external "migrator"

The bootloader (`0x1000`) and partition table (`0x8000`) are protected regions.
The prebuilt Arduino-ESP32 `spi_flash` lib is compiled with
`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS`, so the OpenEVSE app **cannot** write
them (`esp_flash` aborts; the ROM-function workaround can't drive the controller
while the modern driver owns it — see `flash_migrate_worklog.md` for the full
dead-end log). The fix is to do that one protected write from a **tiny separate
ESP-IDF app built with `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y`**, where
plain `esp_flash` writes `0x1000`/`0x8000`/otadata correctly.

That app — the **migrator** — lives at https://github.com/RAR/openevse-16mb-migrator
(submodule `migrator/`). It embeds the 16MB `bootloader.bin` + `partitions.bin`
and, when run, verifies a staged app at `0x650000`, writes+verifies the
bootloader → `0x1000` and partition table → `0x8000`, points otadata → ota_1,
and reboots. `nvs` (`0x9000`) is never touched, so WiFi creds survive.

## The flow

```
OpenEVSE-4MB (this engine, flash_migrate.cpp)
  manifest { app, migrator }
  1. stream 16MB app   -> 0x650000 (ota_1 of the 16MB layout = free flash in 4MB)
                          raw esp_flash; verify SHA-256 + image magic 0xE9
  2. stream migrator   -> esp_ota_get_next_update_partition() (inactive 4MB OTA slot)
                          esp_ota_write; verify SHA-256; esp_ota_set_boot_partition()
  3. reboot
migrator (IDF, DANGEROUS_WRITE_ALLOWED)
  4. verify 0xE9 @0x650000 -> write+verify bl@0x1000 -> write+verify PT@0x8000
     -> otadata -> ota_1 -> reboot
OpenEVSE-16MB boots from ota_1
```

The handoff itself (step 2) is an **ordinary app OTA** — no protected write — so
the running OpenEVSE-4MB stays in its slot as a fallback until the migrator's own
reboot. The only protected write is the verified ~25KB bootloader inside the
audited migrator, before a single partition-table sector flip.

## Manifest (`migrate_v1_16mb.json`)

```json
{
  "app":      { "url": ".../openevse_wifi_v1_16mb.bin", "sha256": "..." },
  "migrator": { "url": ".../openevse_migrator.bin",      "sha256": "..." }
}
```

Firmware default URL: `MIGRATE_MANIFEST_URL` (override with `-D`). The `app` is
the normal 16MB OpenEVSE firmware; the `migrator` carries the bl/PT, so they are
not downloaded separately.

## Endpoints / states

- `POST /migrate/expand16mb` `{ "url": "...", "dry_run": true }` — start (or, with
  `dry_run`, download + SHA-verify app **and** migrator and write nothing).
  `412` if not eligible (needs a 16MB chip on a <=4MB layout), `409` if running.
- `GET /migrate/status` — `state` (`manifest`/`app`/`migrator`/`idle`),
  `dl_pos`/`dl_total`, `failed`/`fail_code`, `free_heap`.
- `/config` exposes `can_expand_16mb` + `partition_scheme` for the GUI button.
- Websocket `migrate` states: `staging`, `downloading_app`, `verifying`,
  `downloading_migrator`, `done`, `dryrun_ok`, `failed`.

## Building the migrator (CI does this per release)

```sh
pio run -e openevse_wifi_v1_16mb                          # the 16MB app
python migrator/scripts/gen_embeds.py .pio/build/openevse_wifi_v1_16mb
( cd migrator && pio run )                                # the migrator (espidf)
```
CI publishes `openevse_wifi_v1_16mb.bin` + `openevse_migrator.bin` +
`migrate_v1_16mb.json` to the release.

## Safety

- The migrator refuses to touch anything unless a valid app image (`0xE9`) is
  staged at `0x650000`; bootloader and PT are read-back-verified.
- `nvs` (creds) and the staged app are never at risk; the device is always
  old-OpenEVSE-or-new, never neither (PoR = one PT sector after a verified bl).
- The migrator boots under the existing OpenEVSE 4MB bootloader (validated).
