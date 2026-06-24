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

## What does NOT work — applying the protected regions (CONCLUSION)

`flash_migrate_early_commit()` runs from `setup()` before WiFi and is meant to
write the staged bootloader → `0x1000`, partition table → `0x8000`, and otadata
→ select `ota_1`, then reboot into the 16MB firmware. **It cannot be made to
work** and field-OTA rewrite of the bootloader/partition table is impractical on
this platform. The device auto-recovers (RTC WDT reset, ~20s) back to 4MB; the
one-shot scratch header is erased first so there is no boot loop, and the
bootloader is never successfully written so there is no corruption.

### Approaches tried (all fail), proven on-device with a serial console

| Approach | Result |
|---|---|
| `esp_flash` / `spi_flash_*` | **abort** — `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS` blocks 0x1000/0x8000 |
| ROM `esp_rom_spiflash_*` + IDF cache guard (IPI stall) | **deadlock**, then **corrupts** the chip |
| ROM + `esp_cpu_stall` + `spi_flash_disable_cache` (both cores) | **hang** |
| ROM + `esp_cpu_stall` + ROM `Cache_Read_Disable` (this core only) | **hang in `esp_rom_spiflash_erase_sector`** |

Serial proof (the instrumented build prints checkpoints via `esp_rom_printf` to
UART0): the trace reaches `[migrate] erase 1000` and then the ROM erase never
returns (clean hang), or — with the IDF guard — emits a burst of UART garbage
(corrupted chip state) before the RTC reset.

**Root cause:** the ROM flash functions are the *only* path that bypasses the
write-protection, but they cannot drive the SPI flash while the app's modern
`esp_flash` driver owns the controller (the chip is left in a mode the ROM
command path can't use). A custom 2nd-stage bootloader does not help either —
you cannot deliver a new bootloader over OTA, which is the whole problem
(circular). `esp_flash` *would* handle the cache/cross-core/watchdog correctly
but refuses these regions and the check is not overridable at runtime.

### The dependable conversion: a one-time USB/serial flash

Convert each affected module over USB with esptool (the canonical 16MB flash):

```
pio run -e openevse_wifi_v1_16mb -t upload --upload-port <PORT>
# or, with the release binaries:
esptool --chip esp32 -p <PORT> -b 460800 write_flash \
  0x1000 bootloader_v1_16mb.bin  0x8000 partitions_v1_16mb.bin \
  0x10000 openevse_wifi_v1_16mb.bin
esptool --chip esp32 -p <PORT> erase_region 0xe000 0x2000   # boot the new app (ota_0)
```

NVS at 0x9000 (WiFi creds) is preserved; the OpenEVSE EEPROM config may reset.

The OTA download/verify/staging pipeline in this engine is solid and reused for
nothing now, but is kept in case a future IDF/bootloader path makes the protected
write feasible. `flash_migrate_early_commit()` and the staging code remain for
that, behind the WIP markers above.

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
