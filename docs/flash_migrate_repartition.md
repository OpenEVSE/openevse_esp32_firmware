# In-place 16MB flash repartition (`flash_migrate`) — status & handoff

Migrates a 16MB ESP32 module that was flashed with the **4MB** partition layout
(`min_spiffs.csv`) to the full **16MB** layout (`openevse_16mb.csv`) over-the-air,
from Settings → Developer Tools → **Flash & storage → Expand partition to 16MB**.

Engine: `src/flash_migrate.{cpp,h}`, endpoint `src/web_server_migrate.cpp`,
detection in `src/app_config.cpp` (`/config` exposes `can_expand_16mb` /
`partition_scheme`). Gated by `-D ENABLE_FLASH_MIGRATE` (on `openevse_wifi_v1`).

## What works (verified on hardware)

- **Detection** — `can_expand_16mb` true only on a 16MB chip running a <=4MB layout.
- **Download + verify** — `migrate_v1_16mb.json` manifest, then bootloader /
  partition-table / app, each SHA256-verified. Downloads are serialized (one TLS
  connection at a time) and driven from `flash_migrate_loop()`, never from a
  mongoose callback. RAM buffers are pre-reserved to avoid `bad_alloc` (growing a
  `std::vector` mid-TLS fragments the heap and panics). Stall watchdog +
  generation counter recover cleanly from a hung transfer.
- **App staging** — the 2MB app streams to `0x650000` (the new `ota_1`) via
  `esp_flash_*` (an ordinary, unprotected region; works while WiFi runs).
- **Commit staging** — the bootloader + partition table are written to a scratch
  region at `0x400000` (also unprotected), with a header (`magic + lengths +
  crc`) written last; then the device reboots.

## What does NOT work yet — applying the protected regions

`flash_migrate_early_commit()` runs from `setup()` before WiFi and is meant to
write the staged bootloader → `0x1000`, partition table → `0x8000`, and otadata
→ select `ota_1`, then reboot into the 16MB firmware. It currently **hangs** in
the ROM flash op and the device is force-reset (RTC WDT, POWERON), then boots
back to 4MB (the one-shot scratch header is erased first, so no boot loop; the
bootloader is never actually written, so no corruption).

### Approaches tried (all fail)

| Approach | Result |
|---|---|
| `esp_flash` / `spi_flash_*` | **abort** — `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS` blocks 0x1000/0x8000 |
| ROM `esp_rom_spiflash_*` + IDF cache guard (IPI stall) | **deadlock** (cross-core) |
| ROM + `esp_cpu_stall` + `spi_flash_disable_cache` | **hang** |
| ROM + `esp_cpu_stall` + ROM `Cache_Read_Disable` | **hang** |

Root cause: the ROM flash functions (the only ones that bypass the write
protection) are incompatible with the running system's SPI-flash-controller
state, and force-stalling the other core while the SMP scheduler is live hangs
the system. Coredump partition does not capture POWERON resets, so remote
debugging is blind here.

### Recommended fix (needs serial console)

Apply the staged commit from a **custom 2nd-stage bootloader** hook: the
bootloader checks the scratch header (`SCRATCH_OFFSET` / `SCRATCH_MAGIC`) and, if
present and CRC-valid, writes `0x1000` / `0x8000` / otadata and clears the header
— all *before* the app and the modern flash driver touch the SPI controller, so
ROM flash access is well-defined and there is no other core to coordinate with.
This is how OTA bootloader-update solutions do it. Develop it on the bench with a
serial console (the panic backtrace and `[migrate]` logs print to the debug UART).

## On-device diagnostics (kept for the serial work)

- `GET /migrate/status` — live state: `state`, `dl_pos/dl_total`, `commit_marker`
  (RTC-persisted; survives a soft reset, wiped by POWERON), `reset_reason`
  (1=POWERON, 3=SW, 4=PANIC, 5=INT_WDT, 6=TASK_WDT), `free_heap`, etc.
- `GET /migrate/coredump` — decoded last panic (task/PC/backtrace) for addr2line.
- `POST /migrate/expand16mb` `{ "url": "...", "dry_run": true }` — `dry_run`
  downloads + verifies everything and writes nothing (no scratch, no commit).
- `g_migrate_marker` values: 20 stage, 25 staged+reboot; 30/31/32/33/34 early
  commit (watchdogs disabled / bootloader / partitions / otadata / done).

Decode a backtrace:
```
xtensa-esp32-elf-addr2line -pfiaC -e .pio/build/openevse_wifi_v1/firmware.elf <addrs...>
```

## CI

`.github/workflows/build.yaml` publishes a self-consistent core-3
`bootloader_v1_16mb.bin` + `partitions_v1_16mb.bin` + `openevse_wifi_v1_16mb.bin`
and a `migrate_v1_16mb.json` manifest (SHA256s) to the release. The `vRePartition`
tag has a test release of these.
