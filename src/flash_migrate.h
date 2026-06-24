#ifndef _FLASH_MIGRATE_H
#define _FLASH_MIGRATE_H

// -------------------------------------------------------------------
// In-place flash repartition: migrate a 16MB module that was flashed
// with the 4MB partition layout (min_spiffs.csv) to the full 16MB
// layout (openevse_16mb.csv), over-the-air.
//
// See docs / plan: the new firmware is staged into the currently unused
// 4-16MB region (ota_1 @ 0x650000 in the 16MB layout) and verified before
// the bootloader + partition table + otadata are rewritten. Settings in
// EEPROM (outside the partition table) and NVS (identical offset in both
// layouts) survive; the LittleFS data partition is reformatted blank.
// -------------------------------------------------------------------

#include <Arduino.h>

// True when the running device has a >=16MB flash chip but is using a
// partition layout that only spans <=4MB, AND the migration engine is
// compiled in (ENABLE_FLASH_MIGRATE). This gates the "Expand to 16MB" button.
bool flash_migrate_can_expand_16mb();

// Best-effort description of the current partition layout for the UI:
// "4mb", "16mb", "other" or "unknown".
const char *flash_migrate_partition_scheme();

// Kick off the OTA repartition to the 16MB layout. `manifest_url` may be empty
// to use the built-in default release manifest. Returns false if it could not
// be started (already running, engine not built in, or not expand-eligible).
// Progress/state is pushed over the status websocket as:
//   {"migrate":"staging|committing|done|failed", "migrate_progress":0-100,
//    "migrate_error":<code>}
bool flash_migrate_start_16mb(const String &manifest_url);

// True while a migration is downloading/verifying/committing.
bool flash_migrate_in_progress();

#endif // _FLASH_MIGRATE_H
