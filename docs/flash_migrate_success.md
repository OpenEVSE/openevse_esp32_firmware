# In-System 4 MB → 16 MB Repartition — Successful Implementation

**Status: working, hardware-validated.** A 16 MB ESP32 WiFi module that was
flashed in the field with the **4 MB** partition layout (`min_spiffs.csv`) can now
expand itself to the full **16 MB** layout (`openevse_16mb.csv`) **entirely over
the air**, from a button in *Settings → Developer Tools → Flash & storage*. No USB,
no opening the enclosure. WiFi credentials and all settings survive.

- One observed end-to-end run: ~75 s, fully OTA, device came back as a healthy
  `openevse_wifi_v1_16mb` (`partition_scheme:"16mb"`, `espflash:16777216`,
  `app0_size:6553600`, `littlefs_size:3538944`, `can_expand_16mb:false`), WiFi
  reconnected on its own.
- **Zero bricks** across every development attempt (~18). That is not luck — it is
  the direct result of the staging-before-commit design described below.

---

## Why this was hard

The whole job is one protected write the running firmware **cannot** do:

- The Arduino `Update` class only writes the *inactive app partition*. It cannot
  write the **bootloader** (`0x1000`), the **partition table** (`0x8000`), or
  **otadata** (`0xe000`) — and those three are exactly what must change to grow
  the layout.
- Dropping to the raw `esp_flash_*` API doesn't help either: the prebuilt Arduino
  `spi_flash` library is compiled with **`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS`**.
  Any write that lands in the bootloader / partition-table / otadata region is
  refused at runtime and **aborts the CPU**. You cannot recompile that flag out of
  a prebuilt library.

So the 4 MB firmware is structurally incapable of committing the new layout. The
solution is to **delegate that one commit** to a purpose-built helper that *was*
compiled to allow it.

---

## The architecture: stage in free space, then hand off to a migrator

There are two cooperating programs:

1. **The OpenEVSE 4 MB firmware** (this repo, `src/flash_migrate.cpp`) — does all
   the network work and all the *non-protected* writes while charging/WiFi keep
   running. It never touches a protected region.
2. **The migrator** (`migrator/` submodule →
   [OpenEVSE/openevse-16mb-migrator](https://github.com/OpenEVSE/openevse-16mb-migrator))
   — a ~130-line ESP-IDF app built with
   **`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y`**. It does *only* the protected
   commit, then reboots into the new firmware. It carries the 16 MB bootloader and
   partition table embedded inside itself.

### The two layouts (verified offsets)

```
                   min_spiffs (current 4 MB)     openevse_16mb (target 16 MB)
bootloader  0x1000   (core-2)                    (core-3)            <- changes
partitions  0x8000                                                   <- changes
otadata     0xe000 / 0x2000                       0xe000 / 0x2000    <- re-pointed
nvs   data  0x9000 / 0x5000                        0x9000 / 0x5000   == SAME (creds survive)
app0  ota_0 0x10000 / 0x1E0000                     0x10000 / 0x640000
app1  ota_1 0x1F0000 / 0x1E0000                    0x650000 / 0x640000
spiffs data 0x3D0000                               0xc90000 / 0x360000
```

The crucial facts that make safe staging possible:

- **`nvs` is at `0x9000` in *both* layouts.** It is never erased or moved, so WiFi
  credentials, MQTT config, and all NVS-stored settings carry over untouched.
  (EEPROM-stored config is likewise outside the partition table.)
- The 16 MB layout's `ota_1` (`0x650000`) and `spiffs` live in the **4–16 MB
  region that the current 4 MB layout doesn't use at all.** Writing there cannot
  corrupt the running system — it's empty space as far as the live firmware is
  concerned.
- Raw `esp_flash_write` to `0x650000` is a *normal*, allowed write (it's nowhere
  near a protected region), so the 4 MB firmware can stream the full 16 MB app
  there while WiFi is up.

---

## The migration sequence

Driven by `flash_migrate_loop()` (a main-loop pump, so downloads never re-enter
the mongoose event loop) — `src/flash_migrate.cpp`:

1. **Manifest** — download `migrate_v1_16mb.json` from the GitHub release. It is
   the `{app, migrator}` schema: each entry has a URL + SHA-256.
2. **Stage the 16 MB app** → stream it to `ota_1` at **`0x650000`** via raw
   `esp_flash`, one 4 KB sector at a time. On completion: verify the streamed
   **SHA-256** *and* the app image magic byte (`0xE9`). Mismatch → abort, nothing
   else has changed.
3. **Stage the migrator** → OTA it into the **inactive 4 MB OTA slot** with the
   normal `esp_ota_*` API (`esp_ota_get_next_update_partition` →
   `esp_ota_begin/write/end`). `esp_ota_end` validates the image; then we verify
   our own **SHA-256** before letting it boot.
4. **Hand off** → `esp_ota_set_boot_partition(migrator)` + reboot.
5. **The migrator runs** (under the *old* 4 MB bootloader) and performs the
   protected commit — see next section.
6. **Reboot into OpenEVSE 16 MB.** First boot formats the blank `spiffs`; NVS +
   EEPROM config intact.

A **dry-run** mode does steps 1–3 (download + full verify of both images) and then
stops, writing nothing and booting nothing — used to exercise the whole risky
download/verify path with zero risk.

---

## Why flashing is safe

The design guarantee is: **at every instant the device can boot, it boots either
the old OpenEVSE-4 MB or the new OpenEVSE-16 MB — never "neither".** That comes
from a strict *verify-everything-before-you-commit* ordering and from keeping all
staging in free space.

| Safety property | How it's achieved |
|---|---|
| **Staging can't corrupt the running system** | The 16 MB app is written to `0x650000` and the migrator to the *inactive* OTA slot — both unused by the live 4 MB firmware. The current bootloader, partition table, otadata, app, and `nvs` are all untouched during staging. |
| **Nothing boots until it's fully verified** | The app is SHA-256 + magic-byte checked; the migrator is `esp_ota_end`-validated **and** SHA-256 checked. Only then is the migrator made bootable. A flipped byte → SHA-256 fail → clean abort, original system still intact. |
| **Power loss during staging is harmless** | If power drops anytime before the migrator is selected as boot partition, the device simply reboots into the unchanged OpenEVSE-4 MB. Re-run the migration. |
| **The migrator double-checks before committing** | It *refuses to write anything* unless it finds the staged app's `0xE9` magic at `0x650000`. No staged app → it aborts having changed nothing. |
| **Every protected write is read-back verified** | `write_verify()` in the migrator erases, writes, then reads the region back and `memcmp`s it. A bad write is caught immediately. |
| **Settings/credentials never move** | `nvs` (`0x9000`) and EEPROM config are outside everything that changes. WiFi reconnects on its own. |
| **Commit order is failure-tolerant** | bootloader → partition table → otadata. The new bootloader is backward-compatible with the partition layout it's about to install, and otadata (the pointer that actually selects the new app) is written **last**. |

The only data that is **intentionally discarded** is the on-device LittleFS
content (energy history, logs, schedules, certificates) — it lives in the `spiffs`
partition that physically moves. This is a documented, accepted loss; the new
firmware formats a blank filesystem on first boot. Settings are *not* in LittleFS,
so they survive.

---

## Where the soft-brick risk actually is

To be precise rather than reassuring: there **is** a genuine point of no return.
It is small, and it is confined entirely to the **migrator's commit**, after the
hand-off reboot. It is *not* in the OpenEVSE firmware and *not* during any of the
download/staging on the live device.

```
  [download + stage + verify app]      <- safe: free space only, fully reversible
  [stage + verify migrator]            <- safe: inactive OTA slot, fully reversible
  [set boot = migrator] + reboot       <- safe: old 4MB still fully intact & bootable
  ----------------------------------------------------------------------------
  migrator: verify staged app magic    <- safe: nothing written yet
  migrator: WRITE bootloader  @0x1000  <-- ┐
  migrator: WRITE part. table @0x8000  <--  │  POINT OF NO RETURN
  migrator: WRITE otadata     @0xe000  <-- ┘  (sub-second, three erase+write pairs)
  ----------------------------------------------------------------------------
  reboot -> OpenEVSE 16MB              <- safe again: new system live
```

**The risk window** is the moment the migrator begins overwriting the bootloader
at `0x1000` until otadata is written. If power is lost **in the middle of the
bootloader write specifically**, the device has a partially-written bootloader and
will not boot → it must be recovered over **USB** (esptool). This window is
inherent to *any* ESP32 repartition — you cannot atomically swap a bootloader —
and is on the order of a few hundred milliseconds.

Mitigations that shrink (but cannot fully eliminate) this window:

- The migrator writes the bootloader **first** and read-back-verifies it before
  moving on. If the bootloader write *completed* but a later step failed, simply
  **re-running the migrator is safe and idempotent** — it re-writes all three
  regions from its embedded copies.
- Because the migrator is itself an OTA app that persists in its slot, a failed
  commit generally **reboots back into the migrator**, which retries the commit
  rather than leaving a half-migrated device.
- The truly unrecoverable case — power cut *during the few-hundred-ms bootloader
  write* — is the only scenario that needs USB recovery. The guidance to the user
  during the commit phase is therefore the standard **"do not power off the
  charger"** warning, shown in a non-dismissible progress modal.

### Summary of risk by phase

| Phase | Runs on | Brick risk if power lost |
|---|---|---|
| Manifest / app / migrator download + verify | OpenEVSE 4 MB (live) | **None** — reboots into unchanged 4 MB |
| `set_boot_partition(migrator)` + reboot | OpenEVSE 4 MB | **None** — old app still selected until otadata changes |
| Migrator verifies staged app | Migrator | **None** — nothing written yet |
| Migrator writes bootloader `0x1000` | Migrator | **Soft-brick** (USB recovery) — the only real window |
| Migrator writes PT `0x8000` / otadata `0xe000` | Migrator | Recoverable by re-running the migrator |
| Reboot into 16 MB | New system | **None** |

---

## How the migrator gets built and shipped (CI)

The migrator must embed the **same** 16 MB bootloader + partition table that the
16 MB app expects, so CI regenerates them from the very build it publishes
(`.github/workflows/build.yaml`, release job):

1. Build all envs, including `openevse_wifi_v1_16mb`; extract its
   `bootloader_v1_16mb.bin` and `partitions_v1_16mb.bin`.
2. `migrator/scripts/gen_embeds.py` turns those two `.bin`s into
   `src/openevse_bl.h` / `src/openevse_pt.h`.
3. `pio run -e esp32dev` in `migrator/` (ESP-IDF, `DANGEROUS_WRITE_ALLOWED`)
   → `openevse_migrator.bin`.
4. Publish `openevse_migrator.bin` + `openevse_wifi_v1_16mb.bin` and a
   `migrate_v1_16mb.json` manifest carrying both URLs and SHA-256s.

The 4 MB firmware's default `MIGRATE_MANIFEST_URL` points at the release with the
**literal-tag** form, `releases/download/<tag>/migrate_v1_16mb.json` (currently
the `vRePartition4MB` prerelease), so a device pulls the migrator and app that
match the firmware it's about to install. The literal-tag form is required
because the CI marks its builds as **prereleases**, and GitHub's
`releases/latest/download/...` redirect never resolves to a prerelease — the
manifest's own asset URLs use this same `releases/download/<tag>/` base, so they
agree.

---

## What survives, what's lost

| Data | Location | After migration |
|---|---|---|
| WiFi credentials | `nvs` @ `0x9000` | **Kept** (partition unchanged) |
| All settings (MQTT, options, etc.) | EEPROM / NVS | **Kept** |
| Energy history, logs, schedules, certificates | LittleFS (`spiffs`) | **Lost** (partition moves; reformatted blank) |
| Firmware | `ota_0`/`ota_1` | Replaced with 16 MB OpenEVSE |

---

## Files

- `src/flash_migrate.cpp` / `.h` — detection (`flash_migrate_partition_scheme`,
  `flash_migrate_can_expand_16mb`) and the download/stage/verify/hand-off engine.
- `migrator/` (submodule) — the ESP-IDF commit app; `src/main.c` is the protected
  commit, `scripts/gen_embeds.py` embeds the bl/PT.
- `.github/workflows/build.yaml` — builds + publishes the migrator and manifest.
- Detection surfaces in `/config` as `partition_scheme` + `can_expand_16mb`; the
  GUI button lives in *Developer Tools → Flash & storage*
  (`gui-nightshift`, `RePartition` branch).

## Credit

The migrator approach — recognising the guard as the compile-time
`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED` Kconfig and delegating the protected
commit to a small IDF app — and the original migrator implementation come from
[openevse-16mb-migrator](https://github.com/OpenEVSE/openevse-16mb-migrator).
