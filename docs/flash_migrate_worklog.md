# 16 MB repartition — work log & postmortem

Goal: take a 16 MB ESP32 module that was mistakenly flashed with the **4 MB**
partition layout (`min_spiffs.csv`) and migrate it to the full **16 MB** layout
(`openevse_16mb.csv`) **over-the-air**, from the running firmware.

## TL;DR

- The OTA **download → verify → stage** pipeline was built and proven on
  hardware: app + bl/PT downloaded and SHA-256 verified, app streamed to `ota_1`
  (`0x650000`).
- The **final step — writing the protected bootloader (`0x1000`) and partition
  table (`0x8000`) from the running Arduino app — is a hard wall**: the prebuilt
  Arduino `spi_flash` lib is compiled with `DANGEROUS_WRITE_ABORTS`, and the ROM
  workaround can't drive the controller while `esp_flash` owns it (serial-proven).
- **RESOLVED (and the right answer):** do that one protected write from a tiny
  separate **ESP-IDF "migrator" app built with `DANGEROUS_WRITE_ALLOWED`**, where
  plain `esp_flash` writes those regions correctly. OpenEVSE-4MB stages the app
  and OTAs the migrator into a slot; on reboot the migrator commits and boots
  16MB. **Hardware-validated fully OTA** (no USB): 4MB → 16MB in ~75 s, WiFi
  creds preserved, migrator boots under the existing OpenEVSE 4MB bootloader.
  See `flash_migrate_repartition.md`.
- (Before the migrator approach, a single module was also converted with a
  one-time **USB esptool flash** to confirm the 16MB target itself boots.)

## The headline number: ~18 on-device migration runs, **0 bricks**

Across roughly **15 firmware build/flash iterations** and **~18 on-device
migration/commit attempts** (a mix of download tests, dry-runs and real
commits), the module **never bricked once**. Every failed attempt left the
4 MB bootloader intact and the device recovered to the 4 MB layout — because the
protected write always either:

- was refused up-front by the IDF (`abort`), or
- hung/crashed **before** any byte of the real bootloader was written, or
- (late in the work) was caught by an RTC-watchdog auto-recovery that resets back
  to 4 MB in ~20 s.

This was by design: the bootloader/partition writes were staged and verified
first, and the commit was structured so an interruption could never leave a
half-written bootloader. The brick-window was never actually entered because the
flash op never got far enough.

## What works (kept in the engine)

| Stage | Status |
|---|---|
| Detect "16 MB chip + 4 MB layout" (`/config` `can_expand_16mb`) | ✅ |
| Download manifest + 3 binaries from GitHub release | ✅ |
| SHA-256 verify each, image magic check | ✅ |
| Stream 2 MB app to `ota_1` (`0x650000`) | ✅ |
| Stage bootloader + partition table to scratch (`0x400000`) + CRC header | ✅ |
| Reboot; apply staged images before WiFi | ❌ (the wall) |

## Challenges encountered (in order)

Each row is a distinct wall that was diagnosed and fixed (or proven a dead end).

| # | Challenge | Symptom | Resolution |
|---|---|---|---|
| 1 | Concurrent TLS connections | bootloader download stalls ~43 % | serialize downloads, one TLS at a time |
| 2 | Mongoose callback re-entrancy | starting the next GET from a callback wedged the event loop | main-loop pump (`flash_migrate_loop`); callbacks only set flags |
| 3 | `std::vector` `bad_alloc` panic | crash mid-app at ~98 % | pre-reserve RAM buffers while heap is unfragmented |
| 4 | Over-reserved RAM | manifest download hung (TLS starved of heap) | per-file buffers, ~40 KB total reserve |
| 5 | Stall-watchdog wedge | a hung transfer left the engine stuck `active` | generation counter + fail-before-`conn_open` |
| 6 | Dangerous-write protection | `esp_flash`/`spi_flash_*` **abort** on `0x1000`/`0x8000` | switch to ROM `esp_rom_spiflash_*` (the only bypass) |
| 7 | ROM funcs + IDF cache guard | cross-core **deadlock** (TASK_WDT, 5 s) | — (guard uses an IPI stall that never ACKs) |
| 8 | Interrupt/task watchdogs | reset during the (long) ROM erase | disable MWDT0/1 during commit |
| 9 | `op_lock` didn't fix the deadlock | still TASK_WDT at the bootloader write | — |
| 10 | Swap guard `is_safe_write_address` | still **abort** — `esp_flash` has its *own* check | — (not overridable at runtime) |
| 11 | Runtime commit hostile to WiFi/SMP | hangs/corruption with the live scheduler | **early-boot commit**: stage to scratch, apply in `setup()` before WiFi |
| 12 | `esp_cpu_stall` + `spi_flash_disable_cache` | hang → POWERON (RTC super-WDT) | — |
| 13 | IRAM lost on inlining | **panic** (IllegalInstruction) at the flash op | `NOINLINE_ATTR` on the IRAM helpers |
| 14 | Disabling the *stalled* core's cache | hang (waits on the frozen core) | disable only the running core's cache |
| 15 | **ROM `esp_rom_spiflash_erase_sector` itself hangs** | serial: reaches `erase 1000` then dead | **none** — the ROM path can't drive the flash while `esp_flash` owns it |
| 16 | RTC watchdog didn't auto-reset | hang required a manual power-cycle | arm RTC WDT (`RESET_SYSTEM`, 20 s) → auto-recover to 4 MB |

### The conclusive finding (with serial proof)

An on-device serial console (instrumented with `esp_rom_printf`, since the
coredump partition doesn't capture POWERON resets) showed the commit reaching
`[migrate] erase 1000` and then the **ROM flash erase never returning** — or,
with the IDF guard, emitting a burst of UART garbage (a corrupted chip state)
before the RTC reset.

Root cause: the ROM flash functions are the *only* way to bypass the
dangerous-write protection, but they cannot drive the SPI flash while the app's
modern `esp_flash` driver owns the controller (the chip is left in a mode the ROM
command path can't use). A custom 2nd-stage bootloader doesn't help either — you
can't deliver a new bootloader over OTA, which is the entire problem (circular).

## Resolution — one-time USB conversion (validated)

```
esptool --chip esp32 -p <PORT> -b 460800 write_flash \
  --flash_mode dio --flash_freq 40m --flash_size 16MB \
  0x1000 bootloader_v1_16mb.bin  0x8000 partitions_v1_16mb.bin \
  0x10000 openevse_wifi_v1_16mb.bin
esptool --chip esp32 -p <PORT> erase_region 0xe000 0x2000   # boot the new app (ota_0)
```

Notes from doing it live:
- The module's USB-serial has **no auto-reset wiring** (esptool saw RAPI `$`
  traffic = `0x24`), so download mode needed a manual **hold BOOT/WiFi (GPIO0) +
  power-cycle**, and a power-cycle afterwards to boot the new firmware.
- NVS (`0x9000`, WiFi credentials) is preserved; OpenEVSE EEPROM config may reset.

## Net takeaway

Field-OTA rewrite of the bootloader + partition table is **not feasible from the
Arduino app itself** (the prebuilt `spi_flash` lib bakes in `DANGEROUS_WRITE_
ABORTS`), but it **is** feasible by delegating that one write to a small external
**ESP-IDF migrator** built with `DANGEROUS_WRITE_ALLOWED`. That is the shipped
solution — the OTA staging engine hands off to the migrator, validated fully OTA
on hardware. See `docs/flash_migrate_repartition.md`. The USB esptool flash above
remains a fine fallback for bench/recovery.

Credit: the migrator approach and implementation are from
https://github.com/RAR/openevse-16mb-migrator.
