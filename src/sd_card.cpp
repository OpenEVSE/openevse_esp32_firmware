#ifdef ENABLE_SD_CARD

#include <Arduino.h>
#include <SD_MMC.h>
#include <driver/gpio.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sd_card.h"
#include "sdlog_store.h"
#include "config_backup.h"
#include "debug.h"

#ifndef SD_PIN_CLK
#define SD_PIN_CLK  38
#endif
#ifndef SD_PIN_CMD
#define SD_PIN_CMD  39
#endif
#ifndef SD_PIN_D0
#define SD_PIN_D0   40
#endif
// Card-detect. -1 disables the check and assumes a card is present.
#ifndef SD_PIN_CD
#define SD_PIN_CD   41
#endif

// 1-bit SDMMC tops out well below the 4-bit ceiling, and this is an append-mostly
// workload at a 32-byte record every 10 s, so there is nothing to gain from
// pushing the clock on unproven wiring.
#ifndef SD_MMC_FREQ_KHZ
#define SD_MMC_FREQ_KHZ 20000
#endif

#ifdef SD_FORMAT_ON_MOUNT_FAIL
#define SD_FORMAT_IF_MOUNT_FAILED true
#else
#define SD_FORMAT_IF_MOUNT_FAILED false
#endif

static bool _mounted = false;
// Set whenever card-detect has read "absent" since the last mount. A mount is
// only attempted on the rising edge of detect, so a card that is present but
// unmountable is tried once, not once a second.
static bool _seen_absent = false;
static uint32_t _generation = 0;
static const char *_status = "not probed";
static uint64_t _size = 0, _used = 0;
static unsigned long _usage_at = 0;
static unsigned long _detect_at = 0;
#define SD_USAGE_REFRESH_MS (5UL * 60UL * 1000UL)
#define SD_DETECT_POLL_MS   1000UL

// Background job: format and/or ring creation. Both are seconds-to-minutes of
// sequential card writes, far too long for the main loop, so they run in a
// throwaway task and the main loop picks up the result.
enum JobKind { JOB_NONE, JOB_CREATE_RING, JOB_FORMAT };
static volatile JobKind _job = JOB_NONE;
static volatile bool _job_done = false;
static volatile bool _job_ok = false;
static bool _ring_gave_up = false;   // ring creation failed on this mount; do not loop on it

// SD_MMC keeps the card handle protected. Forming a pointer-to-member through
// a derived class is the sanctioned way at it; nothing is instantiated.
struct SdCardHandleAccess : fs::SDMMCFS {
  static sdmmc_card_t *card_of(fs::SDMMCFS &fs) { return fs.*(&SdCardHandleAccess::_card); }
};

static void refresh_usage()
{
  _size = SD_MMC.cardSize();
  _used = SD_MMC.usedBytes();
  _usage_at = millis();
}

uint64_t sd_card_size() { return _mounted ? _size : 0; }
uint64_t sd_card_used() { return _mounted ? _used : 0; }

bool sd_card_detected()
{
#if SD_PIN_CD >= 0
  return digitalRead(SD_PIN_CD) == LOW;   // switch shorts to GND on insertion
#else
  return true;
#endif
}

bool sd_card_begin()
{
#if SD_PIN_CD >= 0
  // Internal pull-up is load-bearing on v1.2: the detect switch only pulls down,
  // and R33's pull-up is on DAT3, a different net. Harmless on v1.3, which fits
  // R42 externally. Safe to use pinMode here -- unlike the RAPI RX line, this pin
  // is not attached to a peripheral, so the core-3 peripheral manager has nothing
  // to tear down.
  pinMode(SD_PIN_CD, INPUT_PULLUP);
#endif

  if(!sd_card_detected()) {
    _seen_absent = true;
    _status = "no card";
    DBUGLN("[sd] no card detected");
    return false;
  }

  if(!SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0)) {
    _status = "pin config failed";
    DBUGLN("[sd] setPins failed");
    return false;
  }

  // mode1bit = true: DAT1/DAT2 reach no GPIO on this board.
  // format_if_mount_failed is off unless the build says otherwise (see the
  // header): an unreadable card is a diagnosis, not an invitation to erase
  // whatever the user had on it.
  if(!SD_MMC.begin(SD_MOUNT_POINT, true, SD_FORMAT_IF_MOUNT_FAILED, SD_MMC_FREQ_KHZ)) {
    _status = "mount failed";
    DBUGLN("[sd] mount failed (card present). If this card works elsewhere, check "
           "R33 / DAT3 -- a DAT3 that floats low latches the card into SPI mode");
    return false;
  }

  if(SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    _status = "no card";
    DBUGLN("[sd] mounted but no card type");
    return false;
  }

  _mounted = true;
  _seen_absent = false;
  _ring_gave_up = false;
  _generation++;
  _status = "mounted";
  refresh_usage();
  DBUGF("[sd] mounted, %llu MB, %llu MB used", _size / (1024ULL * 1024ULL), _used / (1024ULL * 1024ULL));

  // A card inserted while running may be a fresh one with nothing on it. This
  // is inert on the boot mount, where the backup is not armed until after the
  // restore decision -- boot gets its mirror from config_backup_arm() instead.
  config_backup_ensure_on_card();

  return true;
}

bool sd_card_mounted()
{
  return _mounted && _job == JOB_NONE;
}

bool sd_card_busy()
{
  return _job != JOB_NONE;
}

uint32_t sd_card_generation()
{
  return _generation;
}

const char *sd_card_status()
{
  return _status;
}

static void unmount(const char *why)
{
  sdlog_store_end();
  SD_MMC.end();
  _mounted = false;
  _status = why;
}

// ---- background job ------------------------------------------------------

static void job_task(void *)
{
  bool ok = true;
  if(_job == JOB_FORMAT) {
    unsigned long t0 = millis();
    esp_err_t err = esp_vfs_fat_sdcard_format(SD_MOUNT_POINT, SdCardHandleAccess::card_of(SD_MMC));
    ok = (err == ESP_OK);
    DBUGF("[sd] format %s in %lu ms", ok ? "done" : "FAILED", millis() - t0);
  }
  if(ok) {
    ok = sdlog_store_preallocate();
  }
  _job_ok = ok;
  _job_done = true;
  vTaskDelete(nullptr);
}

static bool start_job(JobKind kind)
{
  if(_job != JOB_NONE) {
    return false;
  }
  _job_done = false;
  _job_ok = false;
  _job = kind;
  _status = (kind == JOB_FORMAT) ? "formatting" : "creating log";
  // The ring is closed for the duration; the logger falls back to flash.
  sdlog_store_end();
  // 6 KB of stack: FATFS + newlib stdio on the way down to the driver.
  if(xTaskCreate(job_task, "sd_job", 6144, nullptr, 1, nullptr) != pdPASS) {
    DBUGLN("[sd] could not start card job");
    _job = JOB_NONE;
    _status = "mounted";
    return false;
  }
  return true;
}

bool sd_card_request_format()
{
  if(!_mounted || _job != JOB_NONE) {
    return false;
  }
  DBUGLN("[sd] format requested");
  return start_job(JOB_FORMAT);
}

static void finish_job()
{
  JobKind kind = _job;
  bool ok = _job_ok;
  _job = JOB_NONE;
  _job_done = false;

  if(!_mounted) {
    return;   // pulled during the job; the unmount path already spoke
  }
  if(!ok) {
    DBUGF("[sd] %s failed", kind == JOB_FORMAT ? "format" : "ring creation");
    _ring_gave_up = true;
    _status = "mounted";
    return;
  }
  refresh_usage();
  _status = "mounted";
  if(kind == JOB_FORMAT) {
    // The format took the config mirror with it.
    config_backup_to_card();
  }
  if(!sdlog_store_begin()) {
    DBUGLN("[sd] fresh ring would not open");
    _ring_gave_up = true;
  }
}

// ---- main-loop housekeeping ---------------------------------------------

void sd_card_loop()
{
  if(_job != JOB_NONE) {
    if(_job_done) {
      finish_job();
    }
    // Card pulled mid-job: the task's writes fail on their own, and detect is
    // handled below once the job has reported in. Nothing else to do here.
    if(!_job_done) {
      return;
    }
  }

  if(millis() - _detect_at >= SD_DETECT_POLL_MS) {
    _detect_at = millis();
    if(_mounted) {
      if(!sd_card_detected()) {
        // Pulled while live. Close the ring first so no handle outlives its
        // filesystem, then drop the mount so the logger falls back to flash.
        DBUGLN("[sd] card removed, unmounting and falling back to internal flash");
        unmount("removed");
        return;
      }
    } else {
      if(!sd_card_detected()) {
        _seen_absent = true;
        return;
      }
      if(!_seen_absent) {
        // Present but not mounted, and detect never dropped: the earlier mount
        // attempt failed and nothing has changed. Wait for a fresh insertion.
        return;
      }
      DBUGLN("[sd] card inserted, mounting");
      if(!sd_card_begin()) {
        return;
      }
    }
  }

  if(!_mounted) {
    return;
  }

  if(!sdlog_store_ready() && !_ring_gave_up) {
    if(sdlog_store_begin()) {
      // opened an existing ring
    } else if(!sdlog_store_exists()) {
      start_job(JOB_CREATE_RING);
      return;
    } else {
      _ring_gave_up = true;
    }
  }

  if(millis() - _usage_at > SD_USAGE_REFRESH_MS) {
    refresh_usage();
  }
}

#endif // ENABLE_SD_CARD
