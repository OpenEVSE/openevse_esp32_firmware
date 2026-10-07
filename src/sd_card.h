// microSD card presence and mount, for the ESP32-S3 LCD board.
//
// J2 and R33 are fitted (both revisions). The socket is wired for SDMMC 1-bit --
// CLK, CMD and D0 only -- because DAT1/DAT2 reach no GPIO on either version, so
// 1-bit is not a fallback here, it is the only mode.
//
// R33 holds DAT3 high and is functional, not decoration: a DAT3 that floats low
// during init latches the card into SPI mode, where it never answers SDMMC. That
// presents as a dead socket on good hardware, so it is the first thing to check
// if a mount fails on a card that works elsewhere.
//
// Card-detect (IO41) is active low and needs a pull-up -- internal on v1.2,
// external R42 on v1.3. Swapping a card means opening the enclosure (the socket
// mouth is 1.46 mm inboard and push-push needs a full card length of travel), so
// insertion is normally a boot-time fact. Both edges are watched anyway: pulling
// a live card must degrade to internal flash rather than wedge the logger, and
// a card inserted (or re-inserted) later is mounted at the next poll so a bench
// swap does not need a reboot.
#ifndef __SD_CARD_H
#define __SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

#ifndef SD_MOUNT_POINT
#define SD_MOUNT_POINT "/sdcard"
#endif

// Define SD_FORMAT_ON_MOUNT_FAIL to format a card that is present but will not
// mount. Off by default on purpose: on this board an unmountable card is more
// often the DAT3 / R33 wiring than a bad filesystem, and erasing someone's card
// on that diagnosis is the wrong default. Bench builds can turn it on.

#ifdef ENABLE_SD_CARD

// Probe card-detect and, if a card is there, mount it. Safe to call when no card
// is fitted; returns false and leaves the system on internal flash.
bool sd_card_begin();

// True if the card is mounted and has not been pulled since.
bool sd_card_mounted();

// True if the card-detect switch currently reads "card present". Independent of
// whether the mount succeeded, so a card that is physically in but unreadable can
// be told apart from an empty slot in the logs.
bool sd_card_detected();

// Housekeeping, from the main loop. Samples card-detect once a second and acts
// on both edges (unmount on pull, mount on insert), opens the energy-log ring
// on a mounted card (creating it in a background task when the card has
// none), and runs a requested format. Cheap when nothing is happening.
void sd_card_loop();

// Ask for the card to be formatted (FAT, whole card) and re-provisioned with
// a fresh energy-log ring and config mirror. Runs asynchronously from
// sd_card_loop(); watch sd_card_status() for "formatting" -> "creating log"
// -> "mounted". Returns false if there is no mounted card or a job is already
// running.
bool sd_card_request_format();

// True while a format or ring creation is in progress. The store is unusable
// and sd_card_mounted() reports false for the duration.
bool sd_card_busy();

// Incremented on every successful mount.
uint32_t sd_card_generation();

// Human-readable state for /status and the boot log.
const char *sd_card_status();

// Card capacity and FAT usage in bytes (0 when not mounted). usedBytes() walks
// the FAT, which on a 32 GB card is not free, so the figures are cached at
// mount and refreshed from sd_card_loop() at most every few minutes.
uint64_t sd_card_size();
uint64_t sd_card_used();

#else

static inline bool sd_card_begin() { return false; }
static inline bool sd_card_mounted() { return false; }
static inline bool sd_card_detected() { return false; }
static inline void sd_card_loop() { }
static inline bool sd_card_request_format() { return false; }
static inline bool sd_card_busy() { return false; }
static inline uint32_t sd_card_generation() { return 0; }
static inline const char *sd_card_status() { return "unsupported"; }
static inline uint64_t sd_card_size() { return 0; }
static inline uint64_t sd_card_used() { return 0; }

#endif // ENABLE_SD_CARD

#endif // __SD_CARD_H
