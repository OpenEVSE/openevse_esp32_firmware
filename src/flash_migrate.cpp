#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_FLASH_MIGRATE)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "debug.h"
#include "flash_migrate.h"

#if defined(ESP32) && !defined(EPOXY_DUINO)

#include "esp_partition.h"

// openevse_16mb.csv geometry (the target layout). app1/ota_1 and the LittleFS
// partition both live in the currently-unused 4-16MB region, so staging there
// never touches the running 4MB system.
static const uint32_t FLASH_SECTOR          = 4096;
static const uint32_t SIZE_4MB              = 0x400000;
static const uint32_t SIZE_16MB             = 0x1000000;

// ---- detection (available on every ESP32 build) -------------------------

// Highest end-offset of any defined partition. <=4MB means the 4MB layout.
static uint32_t partition_extent()
{
  uint32_t maxend = 0;
  const esp_partition_type_t types[] = { ESP_PARTITION_TYPE_APP, ESP_PARTITION_TYPE_DATA };
  for(esp_partition_type_t type : types)
  {
    esp_partition_iterator_t it = esp_partition_find(type, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while(it)
    {
      const esp_partition_t *p = esp_partition_get(it);
      uint32_t end = p->address + p->size;
      if(end > maxend) {
        maxend = end;
      }
      it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
  }
  return maxend;
}

const char *flash_migrate_partition_scheme()
{
  uint32_t ext = partition_extent();
  if(ext <= SIZE_4MB)  return "4mb";
  if(ext <= SIZE_16MB) return "16mb";
  return "other";
}

#if ENABLE_FLASH_MIGRATE

#include "web_server.h"
#include <MongooseHttpClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <string.h>

#include "esp_flash.h"       // esp_flash_* for the (unprotected) scratch region
#include "esp_rom_crc.h"
#include "mbedtls/sha256.h"
#include "esp_core_dump.h"
#include "esp_attr.h"
#include "esp_spi_flash.h"
#include "rom/spi_flash.h"   // esp_rom_spiflash_* (writes the protected regions)
#include "rom/cache.h"       // Cache_Read_Disable/Enable/Flush (direct, no coord)
#include "esp_cpu.h"         // esp_cpu_stall()/unstall() — hardware core stall
#include "esp_rom_sys.h"     // esp_rom_printf() — early-boot console (UART0)
#include "hal/wdt_hal.h"     // disable the MWDT watchdogs during the commit
#include "soc/timer_group_struct.h"
#include "soc/rtc_wdt.h"     // feed the RTC watchdog (kept as a hang safety-net)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Default release manifest. Points at this repo's GitHub releases, where the CI
// (.github/workflows/build.yaml) publishes migrate_v1_16mb.json together with
// the matching 16MB app/bootloader/partition binaries. Override with
// -D MIGRATE_MANIFEST_URL=... per build env if you publish elsewhere.
#ifndef MIGRATE_MANIFEST_URL
#define MIGRATE_MANIFEST_URL "https://github.com/OpenEVSE/openevse_esp32_firmware/releases/latest/download/migrate_v1_16mb.json"
#endif

// Critical flash regions (identical across the ESP32 family for our boards).
static const uint32_t BOOTLOADER_OFFSET     = 0x1000;
static const uint32_t PARTITION_TABLE_OFFSET = 0x8000;
static const uint32_t PARTITION_TABLE_MAX   = 0xC00;   // 4KB sector holds it
static const uint32_t OTADATA_OFFSET        = 0xE000;
static const uint32_t OTADATA_SIZE          = 0x2000;
static const uint32_t NEW_APP1_OFFSET       = 0x650000; // ota_1 in 16MB layout
static const uint32_t NEW_APP1_SIZE         = 0x640000;
static const uint32_t BOOTLOADER_MAX        = PARTITION_TABLE_OFFSET - BOOTLOADER_OFFSET;

// Scratch staging area in the currently-unused flash above 4MB. The bootloader
// and partition table are written here at runtime (an ordinary, unprotected
// region esp_flash can write while WiFi runs), then applied to the protected
// 0x1000 / 0x8000 regions at the next boot — before WiFi starts, where the ROM
// flash writes can stall the (idle) other core without deadlocking it.
static const uint32_t SCRATCH_OFFSET        = 0x400000; // free in the 4MB layout
static const uint32_t SCRATCH_HEADER_OFFSET = 0x400000; // magic + lengths + crcs
static const uint32_t SCRATCH_BL_OFFSET     = 0x401000; // bootloader (<=28KB)
static const uint32_t SCRATCH_PT_OFFSET     = 0x408000; // partition table (<=3KB)
static const uint32_t SCRATCH_TOTAL         = 0x9000;   // header + bl + pt span
static const uint32_t SCRATCH_MAGIC         = 0x16DB0001;

typedef struct {
  uint32_t magic;
  uint32_t bl_len;
  uint32_t bl_crc;
  uint32_t pt_len;
  uint32_t pt_crc;
} scratch_header_t;

// RAM buffers reserved up front (while the heap is unfragmented) so no buffered
// download ever reallocates mid-TLS-transfer. Each file lands in its own
// buffer. Kept small so an open mbedTLS connection still has room: bootloader
// is <=28KB, partitions 3KB, manifest <1KB; the app streams to flash.
static const size_t MIGRATE_SCRATCH_CAP     = 4 * 1024;   // manifest
static const size_t MIGRATE_BOOTLOADER_CAP  = 30 * 1024;
static const size_t MIGRATE_PARTITIONS_CAP  = 4 * 1024;
static const uint32_t MIGRATE_STALL_MS      = 25000;      // no-progress timeout

enum {
  ST_IDLE = 0,
  ST_MANIFEST,
  ST_BOOTLOADER,
  ST_PARTITIONS,
  ST_APP,
};

struct MigrateCtx
{
  bool active = false;
  bool dry_run = false; // download+verify everything but write nothing / no commit
  int state = ST_IDLE;
  String manifest_url;

  String app_url, bl_url, pt_url;
  String app_sha, bl_sha, pt_sha;

  std::vector<uint8_t> buf;          // current buffered download (manifest/bl/pt)
  std::vector<uint8_t> bootloader;   // verified, held until commit
  std::vector<uint8_t> partitions;   // verified, held until commit

  size_t dl_total = 0;
  size_t dl_pos = 0;
  int last_percent = -1;

  // Downloads are strictly serialized and driven from flash_migrate_loop() (the
  // main loop) — never from inside a mongoose callback. The callbacks only
  // buffer bytes and set these flags; the loop starts/advances connections.
  // This keeps only one mbedTLS client context alive at a time (the device has
  // ~170KB heap; two concurrent TLS clients exhaust it and a transfer stalls)
  // and avoids re-entering the mongoose event loop from within its callbacks.
  bool conn_open = false;       // a request is in flight
  uint32_t conn_gen = 0;        // bumped per request; stale callbacks are ignored
  bool awaiting_result = false; // started a request, result not yet consumed
  bool body_complete = false;   // current resource fully received
  bool has_redirect = false;    // onResponse saw a 3xx; follow redirect_url
  String redirect_url;
  bool has_pending = false;     // next file queued
  String pending_url;
  bool failed = false;          // a callback flagged failure; loop reports it
  int fail_code = 0;
  const char *fail_where = "";
  uint32_t last_progress_ms = 0; // for the stall watchdog
  size_t last_dl_pos = 0;

  // app streaming -> flash
  bool streaming_app = false;
  uint32_t app_flash_off = 0;
  uint8_t sect[FLASH_SECTOR];
  uint32_t sect_fill = 0;
  bool app_seen_first = false;
  uint8_t app_first = 0;
  mbedtls_sha256_context appsha;
  bool appsha_init = false;
};

// Per-request async state (mirrors http_update's HttpUpdateRequestState so the
// onClose handler does not false-fail after a redirect or normal finalize).
struct DlState
{
  bool finalized = false;
  bool redirected = false;
};

static MongooseHttpClient mclient;
static MigrateCtx mctx;

// Survives a software reset / crash (not power loss). Records how far the
// commit got so a crash mid-commit can be pinpointed after the reboot:
//   10 verifying app, 11 app verified, 20 commit start,
//   21 before bootloader write, 22 bootloader done, 23 partitions done,
//   24 otadata done, 25 about to reboot.
RTC_NOINIT_ATTR uint32_t g_migrate_marker;

static void start_download(const String &url);
static void finalize_current();
static void flash_migrate_commit();

// Park the next GET; it is launched from the current connection's onClose.
static void schedule_download(const String &url)
{
  mctx.pending_url = url;
  mctx.has_pending = true;
}

// ---- helpers ------------------------------------------------------------

static void emit_state(const char *s)
{
  StaticJsonDocument<128> e;
  e["migrate"] = s;
  web_server_event(e);
  yield();
}

static void emit_progress(int percent)
{
  if(percent == mctx.last_percent) {
    return;
  }
  mctx.last_percent = percent;
  StaticJsonDocument<128> e;
  e["migrate_progress"] = percent;
  web_server_event(e);
  yield();
}

static void migrate_fail(int err, const char *where)
{
  DEBUG_PORT.printf("[migrate] FAILED at %s err=%d\n", where, err);
  DBUGF("[migrate] FAILED at %s err=%d", where, err);
  mctx.active = false;
  mctx.state = ST_IDLE;
  mctx.streaming_app = false;
  if(mctx.appsha_init) {
    mbedtls_sha256_free(&mctx.appsha);
    mctx.appsha_init = false;
  }
  StaticJsonDocument<128> e;
  e["migrate"] = "failed";
  e["migrate_error"] = err;
  web_server_event(e);
  yield();
}

static void sha256_hex(const uint8_t digest[32], char out[65])
{
  static const char hx[] = "0123456789abcdef";
  for(int i = 0; i < 32; i++) {
    out[i * 2]     = hx[digest[i] >> 4];
    out[i * 2 + 1] = hx[digest[i] & 0x0f];
  }
  out[64] = '\0';
}

static bool sha_equals(const uint8_t digest[32], const String &expected_hex)
{
  if(expected_hex.length() != 64) {
    return false;
  }
  char hex[65];
  sha256_hex(digest, hex);
  return expected_hex.equalsIgnoreCase(hex);
}

static void sha256_buf(const uint8_t *data, size_t len, uint8_t out[32])
{
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts_ret(&c, 0);
  if(len) {
    mbedtls_sha256_update_ret(&c, data, len);
  }
  mbedtls_sha256_finish_ret(&c, out);
  mbedtls_sha256_free(&c);
}

// Erase one sector then write `len` bytes (region freshly erased).
static bool flash_write_sector(uint32_t off, const uint8_t *buf, uint32_t len)
{
  if(esp_flash_erase_region(esp_flash_default_chip, off, FLASH_SECTOR) != ESP_OK) {
    return false;
  }
  if(esp_flash_write(esp_flash_default_chip, buf, off, len) != ESP_OK) {
    return false;
  }
  return true;
}

// ---- download streaming -------------------------------------------------

// Destination buffer for the current buffered (non-app) download. Each file
// lands directly in its own pre-reserved vector — no shared scratch, no copy —
// to keep RAM use (and heap fragmentation) low while a TLS connection is open.
static std::vector<uint8_t> *dl_dest()
{
  switch(mctx.state) {
    case ST_BOOTLOADER: return &mctx.bootloader;
    case ST_PARTITIONS: return &mctx.partitions;
    default:            return &mctx.buf; // manifest
  }
}

static bool consume_chunk(const uint8_t *data, size_t len)
{
  if(mctx.streaming_app)
  {
    if(len) {
      mbedtls_sha256_update_ret(&mctx.appsha, data, len);
    }
    if(!mctx.app_seen_first && len > 0) {
      mctx.app_first = data[0];
      mctx.app_seen_first = true;
    }
    // Dry run: verify the transfer (sha + size) but never touch flash.
    if(mctx.dry_run) {
      mctx.dl_pos += len;
      feedLoopWDT();
      return true;
    }
    size_t i = 0;
    while(i < len)
    {
      size_t take = FLASH_SECTOR - mctx.sect_fill;
      if(take > len - i) {
        take = len - i;
      }
      memcpy(mctx.sect + mctx.sect_fill, data + i, take);
      mctx.sect_fill += take;
      i += take;
      if(mctx.sect_fill == FLASH_SECTOR)
      {
        if(mctx.app_flash_off + FLASH_SECTOR > NEW_APP1_OFFSET + NEW_APP1_SIZE) {
          return false; // image larger than the target partition
        }
        if(!flash_write_sector(mctx.app_flash_off, mctx.sect, FLASH_SECTOR)) {
          return false;
        }
        mctx.app_flash_off += FLASH_SECTOR;
        mctx.sect_fill = 0;
      }
    }
  }
  else
  {
    // Never reallocate here: growing the vector mid-download fragments and can
    // throw bad_alloc -> terminate -> panic. The destination was reserved up
    // front (heap clean); a file bigger than that is a hard failure instead.
    std::vector<uint8_t> *dst = dl_dest();
    if(dst->size() + len > dst->capacity()) {
      return false;
    }
    dst->insert(dst->end(), data, data + len);
  }

  mctx.dl_pos += len;
  feedLoopWDT();
  return true;
}

static bool finalize_app_image()
{
  // Flush the final partial sector (pad with 0xFF, region is erased anyway).
  if(mctx.sect_fill > 0 && !mctx.dry_run)
  {
    memset(mctx.sect + mctx.sect_fill, 0xFF, FLASH_SECTOR - mctx.sect_fill);
    if(!flash_write_sector(mctx.app_flash_off, mctx.sect, FLASH_SECTOR)) {
      return false;
    }
    mctx.app_flash_off += FLASH_SECTOR;
    mctx.sect_fill = 0;
  }

  uint8_t dig[32];
  mbedtls_sha256_finish_ret(&mctx.appsha, dig);
  mbedtls_sha256_free(&mctx.appsha);
  mctx.appsha_init = false;

  if(mctx.app_first != 0xE9) {
    DEBUG_PORT.println("[migrate] app image bad magic");
    return false;
  }
  if(!sha_equals(dig, mctx.app_sha)) {
    DEBUG_PORT.println("[migrate] app image sha mismatch");
    return false;
  }
  return true;
}

static void finalize_current()
{
  switch(mctx.state)
  {
    case ST_MANIFEST:
    {
      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, mctx.buf.data(), mctx.buf.size());
      if(err) {
        migrate_fail(-20, "manifest json");
        return;
      }
      mctx.bl_url  = (const char *)(doc["bootloader"]["url"]    | "");
      mctx.bl_sha  = (const char *)(doc["bootloader"]["sha256"] | "");
      mctx.pt_url  = (const char *)(doc["partitions"]["url"]    | "");
      mctx.pt_sha  = (const char *)(doc["partitions"]["sha256"] | "");
      mctx.app_url = (const char *)(doc["app"]["url"]           | "");
      mctx.app_sha = (const char *)(doc["app"]["sha256"]        | "");
      if(mctx.bl_url.length() == 0 || mctx.pt_url.length() == 0 || mctx.app_url.length() == 0 ||
         mctx.bl_sha.length() != 64 || mctx.pt_sha.length() != 64 || mctx.app_sha.length() != 64) {
        migrate_fail(-21, "manifest fields");
        return;
      }
      emit_state("staging");
      mctx.state = ST_BOOTLOADER;
      schedule_download(mctx.bl_url);
      break;
    }

    case ST_BOOTLOADER:
    {
      // Downloaded directly into mctx.bootloader; held until commit.
      if(mctx.bootloader.empty() || mctx.bootloader[0] != 0xE9) {
        migrate_fail(-22, "bootloader magic");
        return;
      }
      if(mctx.bootloader.size() > BOOTLOADER_MAX) {
        migrate_fail(-27, "bootloader too large");
        return;
      }
      uint8_t dig[32];
      sha256_buf(mctx.bootloader.data(), mctx.bootloader.size(), dig);
      if(!sha_equals(dig, mctx.bl_sha)) {
        migrate_fail(-23, "bootloader sha");
        return;
      }
      mctx.state = ST_PARTITIONS;
      schedule_download(mctx.pt_url);
      break;
    }

    case ST_PARTITIONS:
    {
      // Downloaded directly into mctx.partitions; held until commit.
      if(mctx.partitions.size() < 96 || mctx.partitions.size() > PARTITION_TABLE_MAX) {
        migrate_fail(-25, "partition table size");
        return;
      }
      uint8_t dig[32];
      sha256_buf(mctx.partitions.data(), mctx.partitions.size(), dig);
      if(!sha_equals(dig, mctx.pt_sha)) {
        migrate_fail(-24, "partition table sha");
        return;
      }
      emit_state("downloading_app");
      mctx.state = ST_APP;
      schedule_download(mctx.app_url);
      break;
    }

    case ST_APP:
    {
      emit_state("verifying");
      g_migrate_marker = 10;
      if(!finalize_app_image()) {
        migrate_fail(-26, "app verify");
        return;
      }
      g_migrate_marker = 11;
      if(mctx.dry_run) {
        // Everything downloaded + verified; stop before touching flash.
        DEBUG_PORT.println("[migrate] dry run OK - all images verified, nothing written");
        mctx.active = false;
        mctx.state = ST_IDLE;
        emit_state("dryrun_ok");
        return;
      }
      flash_migrate_commit();
      break;
    }

    default:
      break;
  }
}

// Flag a failure from inside a callback; flash_migrate_loop() reports it.
static void flag_fail(int code, const char *where)
{
  if(!mctx.failed) {
    mctx.failed = true;
    mctx.fail_code = code;
    mctx.fail_where = where;
  }
}

// The current in-flight request (so the loop can force-close it). The client
// deletes the request object on close, so this is nulled in onClose.
static MongooseHttpClientRequest *current_req = NULL;

// Start one GET. ONLY called from flash_migrate_loop() (main-loop context),
// never from a mongoose callback. Callbacks below only buffer + set flags.
static void start_download(const String &url)
{
  DBUGF("[migrate] GET %s", url.c_str());

  // Clear only the destination for this download (keeps already-downloaded
  // bootloader/partitions intact, and preserves each vector's reserved cap).
  dl_dest()->clear();
  mctx.dl_total = 0;
  mctx.dl_pos = 0;
  mctx.last_dl_pos = 0;
  mctx.last_percent = -1;
  mctx.body_complete = false;
  mctx.has_redirect = false;
  mctx.last_progress_ms = millis();

  if(mctx.state == ST_APP)
  {
    mctx.streaming_app = true;
    mctx.app_flash_off = NEW_APP1_OFFSET;
    mctx.sect_fill = 0;
    mctx.app_seen_first = false;
    mctx.app_first = 0;
    if(mctx.appsha_init) {
      mbedtls_sha256_free(&mctx.appsha);
    }
    mbedtls_sha256_init(&mctx.appsha);
    mbedtls_sha256_starts_ret(&mctx.appsha, 0);
    mctx.appsha_init = true;
  }
  else
  {
    mctx.streaming_app = false;
  }

  MongooseHttpClientRequest *request = mclient.beginRequest(url.c_str());
  if(!request) {
    flag_fail(-10, "beginRequest");
    return;
  }
  current_req = request;
  uint32_t gen = ++mctx.conn_gen; // events from older connections are ignored
  mctx.conn_open = true;
  mctx.awaiting_result = true;
  request->setMethod(HTTP_GET);

  request->onBody([gen](MongooseHttpClientResponse *response)
  {
    if(gen != mctx.conn_gen) return; // stale (aborted) connection
    int code = response->respCode();
    if(code == 200)
    {
      if(mctx.dl_total == 0) {
        mctx.dl_total = response->contentLength();
      }
      if(!consume_chunk((const uint8_t *)response->body().c_str(), response->body().length())) {
        flag_fail(-11, "flash write");
      } else if(mctx.dl_total > 0 && mctx.dl_pos >= mctx.dl_total) {
        mctx.body_complete = true;
      }
    }
    else if(code >= 300 && code < 400)
    {
      return; // redirect handled in onResponse
    }
    else
    {
      flag_fail(code, "http status");
    }
  });

  request->onResponse([gen](MongooseHttpClientResponse *response)
  {
    if(gen != mctx.conn_gen) return;
    int code = response->respCode();
    if(301 == code || 302 == code)
    {
      MongooseString location = response->headers("Location");
      mctx.redirect_url = location.toString();
      mctx.has_redirect = true;
    }
    else if(200 == code)
    {
      if(!mctx.body_complete)
      {
        // Small responses can arrive whole in the final reply with no prior
        // chunk callback; consume whatever body is present.
        size_t len = response->body().length();
        if(len > 0 && !consume_chunk((const uint8_t *)response->body().c_str(), len)) {
          flag_fail(-11, "flash write");
        }
        mctx.body_complete = true;
      }
    }
  });

  request->onClose([gen]()
  {
    if(gen != mctx.conn_gen) return; // a newer request already superseded this
    mctx.conn_open = false;
    current_req = NULL;
  });

  mclient.send(request);
}

// Main-loop pump. Drives the whole download/verify/commit sequence so that
// connections are only ever started from here, not from mongoose callbacks.
void flash_migrate_loop()
{
  if(!mctx.active) {
    return;
  }

  // A failure flagged anywhere (callback or stall watchdog) ends the migration
  // now, even if the connection is wedged and hasn't closed. Bumping conn_gen
  // makes any late events from the aborted connection no-ops.
  if(mctx.failed)
  {
    if(current_req) {
      current_req->abort();
    }
    current_req = NULL;
    mctx.conn_open = false;
    mctx.conn_gen++;
    migrate_fail(mctx.fail_code, mctx.fail_where);
    return;
  }

  if(mctx.conn_open)
  {
    if(mctx.dl_total > 0) {
      int pct = (int)((mctx.dl_pos * 100) / mctx.dl_total);
      emit_progress(pct > 100 ? 100 : pct);
    }
    // Stall watchdog: a hung handshake or stalled transfer must not wedge the
    // migration forever. Abort + flag failure if no bytes for a while.
    if(mctx.dl_pos != mctx.last_dl_pos) {
      mctx.last_dl_pos = mctx.dl_pos;
      mctx.last_progress_ms = millis();
    } else if(millis() - mctx.last_progress_ms > MIGRATE_STALL_MS) {
      flag_fail(-14, "download stalled");
      if(current_req) {
        current_req->abort();
      }
    }
    // Got the whole body but the server is holding the socket open: force the
    // close so its mbedTLS context frees before the next file.
    if(mctx.body_complete && current_req) {
      current_req->abort();
    }
    return;
  }

  // No connection open — act on the result of the one that just closed.
  if(mctx.has_redirect) {
    mctx.has_redirect = false;
    start_download(mctx.redirect_url); // same state
    return;
  }
  if(mctx.body_complete) {
    mctx.body_complete = false;
    mctx.awaiting_result = false;
    finalize_current(); // verify + advance state + schedule next, or commit
    return;
  }
  if(mctx.awaiting_result) {
    mctx.awaiting_result = false;
    migrate_fail(-12, "connection closed");
    return;
  }
  if(mctx.has_pending) {
    mctx.has_pending = false;
    start_download(mctx.pending_url);
    return;
  }
}

// ---- runtime: stage the protected-region images to scratch flash -----------
//
// The bootloader (0x1000) and partition table (0x8000) are "dangerous" regions
// the IDF flash API refuses to write at runtime, and the ROM functions that
// bypass that check deadlock against the modern esp_flash driver that WiFi uses
// on the other core. So at runtime we only write the SCRATCH region (ordinary
// flash above 4MB) — which esp_flash handles fine while WiFi runs — and apply
// it to the protected regions during the next boot, before WiFi starts.

static bool flash_stage_to_scratch()
{
  // Erase the whole scratch span, then write bl + pt, then the header last so a
  // valid header means "fully staged, commit on next boot".
  if(esp_flash_erase_region(esp_flash_default_chip, SCRATCH_OFFSET, SCRATCH_TOTAL) != ESP_OK) {
    return false;
  }
  feedLoopWDT();
  uint32_t bl_w = (mctx.bootloader.size() + 3) & ~3u;
  uint32_t pt_w = (mctx.partitions.size() + 3) & ~3u;
  if(esp_flash_write(esp_flash_default_chip, mctx.bootloader.data(), SCRATCH_BL_OFFSET, bl_w) != ESP_OK) {
    return false;
  }
  feedLoopWDT();
  if(esp_flash_write(esp_flash_default_chip, mctx.partitions.data(), SCRATCH_PT_OFFSET, pt_w) != ESP_OK) {
    return false;
  }
  feedLoopWDT();
  scratch_header_t h;
  h.magic  = SCRATCH_MAGIC;
  h.bl_len = mctx.bootloader.size();
  h.bl_crc = esp_rom_crc32_le(UINT32_MAX, mctx.bootloader.data(), mctx.bootloader.size());
  h.pt_len = mctx.partitions.size();
  h.pt_crc = esp_rom_crc32_le(UINT32_MAX, mctx.partitions.data(), mctx.partitions.size());
  if(esp_flash_write(esp_flash_default_chip, &h, SCRATCH_HEADER_OFFSET, sizeof(h)) != ESP_OK) {
    return false;
  }
  return true;
}

static void flash_migrate_commit()
{
  emit_state("committing");
  g_migrate_marker = 20;
  DEBUG_PORT.println("[migrate] staging commit images to scratch flash");
  if(!flash_stage_to_scratch()) {
    migrate_fail(-33, "stage to scratch");
    return;
  }
  g_migrate_marker = 24;
  mctx.active = false;
  mctx.state = ST_IDLE;
  emit_state("done");
  g_migrate_marker = 25;
  DEBUG_PORT.println("[migrate] staged; rebooting to apply 16MB layout - DO NOT POWER OFF");
  restart_system();
}

// ---- early boot: apply the staged images to the protected regions ----------
//
// !!! KNOWN ISSUE / WORK IN PROGRESS !!!
// Everything up to and including staging works (download, verify, app -> ota_1,
// bootloader+partition-table -> scratch flash, reboot). Applying the staged
// images to the protected 0x1000 / 0x8000 regions does NOT work yet and is the
// remaining task — see docs/flash_migrate_repartition.md. Symptom: the device
// hangs in the ROM flash op below and is force-reset (POWERON) by the RTC
// watchdog, then boots back to the 4MB layout (no damage; the bootloader is
// never reached for an actual write). The robust fix is to apply the commit
// from a custom 2nd-stage bootloader (before the OS/flash driver touch the SPI
// controller), which needs on-device serial debugging to develop safely.
//
// Runs from setup() before WiFi/networking starts. To write the protected
// regions with the ROM flash functions we must disable the flash cache while
// the op runs. The IDF cache guard stalls the other core via an inter-core IPI
// handshake, which deadlocks here (the other core never acknowledges), so we
// stall it in HARDWARE with esp_cpu_stall() instead. These helpers MUST be
// IRAM-resident: the cache is disabled between begin()/end().

// NOINLINE is essential: if these get inlined into a flash-resident caller the
// IRAM placement is lost and executing them with the cache disabled faults.
static void IRAM_ATTR NOINLINE_ATTR flash_op_begin()
{
  int me = xPortGetCoreID();
  esp_cpu_stall(me ? 0 : 1); // hardware-freeze the other core
  portDISABLE_INTERRUPTS();
  // Only touch THIS core's cache. Disabling the stalled core's cache hangs
  // (it waits for the frozen core); the other core won't access flash while
  // frozen, and the regions we write are not XIP-executed by it.
  Cache_Read_Disable(me);
}

static void IRAM_ATTR NOINLINE_ATTR flash_op_end()
{
  int me = xPortGetCoreID();
  Cache_Flush(me); // invalidate any stale cache lines for the just-written flash
  Cache_Read_Enable(me);
  portENABLE_INTERRUPTS();
  esp_cpu_unstall(me ? 0 : 1);
}

static bool IRAM_ATTR NOINLINE_ATTR raw_flash_erase_sector(uint32_t sector)
{
  flash_op_begin();
  esp_rom_spiflash_result_t r = esp_rom_spiflash_erase_sector(sector);
  flash_op_end();
  return r == ESP_ROM_SPIFLASH_RESULT_OK;
}

// src 4-byte aligned, len a multiple of 4.
static bool IRAM_ATTR NOINLINE_ATTR raw_flash_write(uint32_t addr, const uint32_t *src, uint32_t len)
{
  flash_op_begin();
  esp_rom_spiflash_result_t r = esp_rom_spiflash_write(addr, src, (int32_t)len);
  flash_op_end();
  return r == ESP_ROM_SPIFLASH_RESULT_OK;
}

static bool IRAM_ATTR NOINLINE_ATTR raw_flash_read(uint32_t addr, uint32_t *dst, uint32_t len)
{
  flash_op_begin();
  esp_rom_spiflash_result_t r = esp_rom_spiflash_read(addr, dst, (int32_t)len);
  flash_op_end();
  return r == ESP_ROM_SPIFLASH_RESULT_OK;
}

static bool raw_verify(uint32_t addr, const uint8_t *data, uint32_t len)
{
  uint32_t off = 0;
  uint8_t tmp[256];
  while(off < len) {
    uint32_t n = len - off;
    if(n > sizeof(tmp)) n = sizeof(tmp);
    if(!raw_flash_read(addr + off, (uint32_t *)tmp, (n + 3) & ~3u)) return false;
    if(memcmp(tmp, data + off, n) != 0) return false;
    off += n;
  }
  return true;
}

static bool raw_write_region(uint32_t off, const uint8_t *data, uint32_t len)
{
  uint32_t a0 = off & ~(FLASH_SECTOR - 1);
  uint32_t a1 = (off + len + FLASH_SECTOR - 1) & ~(FLASH_SECTOR - 1);
  uint32_t wlen = (len + 3) & ~3u;
  for(int attempt = 0; attempt < 3; attempt++) {
    bool ok = true;
    for(uint32_t a = a0; a < a1 && ok; a += FLASH_SECTOR) {
      esp_rom_printf("[migrate] erase %x\n", a);
      ok = raw_flash_erase_sector(a / FLASH_SECTOR);
      rtc_wdt_feed(); // each op is short; feed the RTC WDT between them
    }
    // Write a sector at a time so a single cache-disabled window stays short.
    for(uint32_t w = 0; w < wlen && ok; w += FLASH_SECTOR) {
      uint32_t n = wlen - w;
      if(n > FLASH_SECTOR) n = FLASH_SECTOR;
      esp_rom_printf("[migrate] write %x\n", off + w);
      ok = raw_flash_write(off + w, (const uint32_t *)(data + w), n);
      rtc_wdt_feed();
    }
    if(ok && raw_verify(off, data, len)) {
      return true;
    }
  }
  return false;
}

// otadata select entry layout (esp_ota_select_entry_t).
typedef struct {
  uint32_t ota_seq;
  uint8_t  seq_label[20];
  uint32_t ota_state;
  uint32_t crc;          // CRC32 of ota_seq only
} ota_select_entry_t;

// Select ota_1 (the staged 16MB firmware): with 2 OTA apps, boot index =
// (ota_seq - 1) % 2, so ota_seq=2 -> ota_1. ota_0 keeps the old firmware.
static bool raw_write_otadata_ota1()
{
  if(!raw_flash_erase_sector(OTADATA_OFFSET / FLASH_SECTOR)) return false;
  rtc_wdt_feed();
  if(!raw_flash_erase_sector((OTADATA_OFFSET + FLASH_SECTOR) / FLASH_SECTOR)) return false;
  rtc_wdt_feed();
  ota_select_entry_t s;
  memset(&s, 0xFF, sizeof(s));
  s.ota_seq = 2;
  s.ota_state = 0xFFFFFFFF; // ESP_OTA_IMG_UNDEFINED -> no rollback verification
  s.crc = esp_rom_crc32_le(UINT32_MAX, (const uint8_t *)&s.ota_seq, sizeof(s.ota_seq));
  if(!raw_flash_write(OTADATA_OFFSET, (const uint32_t *)&s, sizeof(s))) return false;
  return raw_verify(OTADATA_OFFSET, (const uint8_t *)&s, sizeof(s));
}

static void prepare_watchdogs_for_commit()
{
  // Disable the interrupt watchdog (MWDT1 / TIMERG1) and task watchdog
  // (MWDT0 / TIMERG0) — the per-sector cache-disabled windows would otherwise
  // trip them. The RTC watchdog is KEPT as a last-resort hang safety-net (so a
  // bug can't hang the device forever); give it a generous timeout and feed it
  // between each (short) flash op.
  wdt_hal_context_t iwdt;
  iwdt.inst = WDT_MWDT1;
  iwdt.mwdt_dev = &TIMERG1;
  wdt_hal_write_protect_disable(&iwdt);
  wdt_hal_disable(&iwdt);
  wdt_hal_write_protect_enable(&iwdt);

  wdt_hal_context_t twdt;
  twdt.inst = WDT_MWDT0;
  twdt.mwdt_dev = &TIMERG0;
  wdt_hal_write_protect_disable(&twdt);
  wdt_hal_disable(&twdt);
  wdt_hal_write_protect_enable(&twdt);

  rtc_wdt_protect_off();
  rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
  rtc_wdt_set_time(RTC_WDT_STAGE0, 20000); // hang -> auto reset in 20s (-> 4MB)
  rtc_wdt_feed();
  rtc_wdt_enable();
  rtc_wdt_protect_on();
}

void flash_migrate_early_commit()
{
  scratch_header_t h;
  if(esp_flash_read(esp_flash_default_chip, &h, SCRATCH_HEADER_OFFSET, sizeof(h)) != ESP_OK) {
    return;
  }
  if(h.magic != SCRATCH_MAGIC) {
    return; // nothing staged
  }
  if(h.bl_len == 0 || h.bl_len > BOOTLOADER_MAX || h.pt_len < 96 || h.pt_len > PARTITION_TABLE_MAX) {
    return;
  }

  // Use esp_rom_printf: it writes straight to the console UART (UART0) so the
  // checkpoints below appear on the programming serial port even this early in
  // boot, and it is safe to call while flash/cache work is in progress.
  esp_rom_printf("\n[migrate] staged commit found (bl=%u pt=%u), applying before WiFi\n",
                 h.bl_len, h.pt_len);

  uint32_t bl_w = (h.bl_len + 3) & ~3u;
  uint32_t pt_w = (h.pt_len + 3) & ~3u;
  uint8_t *bl = (uint8_t *)malloc(bl_w);
  uint8_t *pt = (uint8_t *)malloc(pt_w);
  if(!bl || !pt) { esp_rom_printf("[migrate] malloc failed\n"); free(bl); free(pt); return; }

  bool ok = (esp_flash_read(esp_flash_default_chip, bl, SCRATCH_BL_OFFSET, bl_w) == ESP_OK) &&
            (esp_flash_read(esp_flash_default_chip, pt, SCRATCH_PT_OFFSET, pt_w) == ESP_OK) &&
            esp_rom_crc32_le(UINT32_MAX, bl, h.bl_len) == h.bl_crc &&
            esp_rom_crc32_le(UINT32_MAX, pt, h.pt_len) == h.pt_crc &&
            bl[0] == 0xE9;
  esp_rom_printf("[migrate] images read+crc: %s\n", ok ? "OK" : "BAD");

  // One-shot: invalidate the header now so a crash mid-commit can't boot-loop.
  esp_flash_erase_region(esp_flash_default_chip, SCRATCH_HEADER_OFFSET, FLASH_SECTOR);

  if(!ok) {
    free(bl); free(pt);
    return;
  }

  // Point of no return. WiFi is not up yet, so the hardware core-stall is safe.
  g_migrate_marker = 30;
  esp_rom_printf("[migrate] disabling watchdogs\n");
  prepare_watchdogs_for_commit();

  g_migrate_marker = 31;
  esp_rom_printf("[migrate] writing bootloader @0x1000 ...\n");
  bool committed = raw_write_region(BOOTLOADER_OFFSET, bl, h.bl_len);
  esp_rom_printf("[migrate] bootloader: %s\n", committed ? "OK" : "FAIL");
  if(committed) {
    g_migrate_marker = 32;
    esp_rom_printf("[migrate] writing partition table @0x8000 ...\n");
    committed = raw_write_region(PARTITION_TABLE_OFFSET, pt, h.pt_len);
    esp_rom_printf("[migrate] partitions: %s\n", committed ? "OK" : "FAIL");
  }
  if(committed) {
    g_migrate_marker = 33;
    esp_rom_printf("[migrate] writing otadata ...\n");
    committed = raw_write_otadata_ota1();
    esp_rom_printf("[migrate] otadata: %s\n", committed ? "OK" : "FAIL");
  }
  if(committed) { g_migrate_marker = 34; }

  free(bl); free(pt);

  esp_rom_printf("[migrate] commit %s, rebooting\n", committed ? "COMPLETE" : "FAILED");
  esp_restart();
}

// ---- public API ---------------------------------------------------------

bool flash_migrate_in_progress()
{
  return mctx.active;
}

void flash_migrate_status_json(JsonDocument &doc)
{
  static const char *names[] = { "idle", "manifest", "bootloader", "partitions", "app" };
  doc["active"] = mctx.active;
  doc["state"] = (mctx.state >= 0 && mctx.state <= ST_APP) ? names[mctx.state] : "?";
  doc["conn_open"] = mctx.conn_open;
  doc["awaiting"] = mctx.awaiting_result;
  doc["body_complete"] = mctx.body_complete;
  doc["has_redirect"] = mctx.has_redirect;
  doc["has_pending"] = mctx.has_pending;
  doc["dl_pos"] = (uint32_t)mctx.dl_pos;
  doc["dl_total"] = (uint32_t)mctx.dl_total;
  doc["app_off"] = mctx.app_flash_off;
  doc["failed"] = mctx.failed;
  doc["fail_code"] = mctx.fail_code;
  doc["fail_where"] = mctx.fail_where;
  doc["free_heap"] = (uint32_t)ESP.getFreeHeap();
  doc["commit_marker"] = g_migrate_marker;       // survives a crash/reboot
  doc["reset_reason"] = (int)esp_reset_reason();  // 1=POWERON 3=SW 4=PANIC 6=WDT...
}

// Decode the last panic's core dump (written to the coredump partition) into
// the crashing task, exception PC and backtrace, for offline addr2line.
void flash_migrate_coredump_json(JsonDocument &doc)
{
  esp_core_dump_summary_t *s = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if(!s) {
    doc["err"] = "nomem";
    return;
  }
  if(esp_core_dump_get_summary(s) == ESP_OK)
  {
    char buf[12];
    doc["task"] = s->exc_task;
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->exc_pc);
    doc["pc"] = String(buf);
    JsonArray bt = doc.createNestedArray("bt");
    for(uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
      snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->exc_bt_info.bt[i]);
      bt.add(String(buf));
    }
    doc["corrupted"] = s->exc_bt_info.corrupted;
  }
  else
  {
    doc["coredump"] = "none";
  }
  free(s);
}

bool flash_migrate_can_expand_16mb()
{
  if(mctx.active) {
    return false;
  }
  return ESP.getFlashChipSize() >= SIZE_16MB && partition_extent() <= SIZE_4MB;
}

bool flash_migrate_start_16mb(const String &manifest_url, bool dry_run)
{
  if(mctx.active) {
    return false;
  }
  if(!flash_migrate_can_expand_16mb()) {
    return false;
  }
  mctx.dry_run = dry_run;

  mctx.app_url = ""; mctx.bl_url = ""; mctx.pt_url = "";
  mctx.app_sha = ""; mctx.bl_sha = ""; mctx.pt_sha = "";
  mctx.buf.clear();
  mctx.bootloader.clear();
  mctx.partitions.clear();
  // Reserve scratch now, while the heap is unfragmented. Doing this lazily
  // during a TLS download throws bad_alloc -> panic (the heap is fragmented by
  // the active mbedTLS connection). bad_alloc is caught so a genuinely full
  // heap fails cleanly instead of crashing.
  try {
    mctx.buf.reserve(MIGRATE_SCRATCH_CAP);
    mctx.bootloader.reserve(MIGRATE_BOOTLOADER_CAP);
    mctx.partitions.reserve(MIGRATE_PARTITIONS_CAP);
  } catch(...) {
    DEBUG_PORT.println("[migrate] could not reserve scratch buffers");
    return false;
  }
  mctx.dl_total = 0;
  mctx.dl_pos = 0;
  mctx.last_percent = -1;
  mctx.streaming_app = false;
  mctx.sect_fill = 0;
  mctx.app_seen_first = false;
  mctx.conn_open = false;
  mctx.awaiting_result = false;
  mctx.body_complete = false;
  mctx.has_redirect = false;
  mctx.failed = false;
  mctx.has_pending = false;
  mctx.pending_url = "";
  if(mctx.appsha_init) {
    mbedtls_sha256_free(&mctx.appsha);
    mctx.appsha_init = false;
  }

  g_migrate_marker = 0;
  mctx.manifest_url = manifest_url.length() ? manifest_url : String(MIGRATE_MANIFEST_URL);
  mctx.state = ST_MANIFEST;
  mctx.active = true;

  DEBUG_PORT.printf("[migrate] starting 16MB expand, manifest=%s\n", mctx.manifest_url.c_str());
  emit_state("staging");
  // The actual GET is launched from flash_migrate_loop() (main-loop context),
  // not here, so no connection is opened from the web request handler.
  schedule_download(mctx.manifest_url);
  return true;
}

#else // ENABLE_FLASH_MIGRATE

// Engine not compiled in: detection still reports the scheme but expansion is
// unavailable.
bool flash_migrate_can_expand_16mb() { return false; }
bool flash_migrate_in_progress() { return false; }
bool flash_migrate_start_16mb(const String &, bool) { return false; }
void flash_migrate_loop() {}
void flash_migrate_early_commit() {}
void flash_migrate_status_json(JsonDocument &doc) { doc["active"] = false; }
void flash_migrate_coredump_json(JsonDocument &doc) { doc["coredump"] = "n/a"; }

#endif // ENABLE_FLASH_MIGRATE

#else // ESP32 && !EPOXY_DUINO

// Host / non-ESP32 builds: stubs.
bool flash_migrate_can_expand_16mb() { return false; }
const char *flash_migrate_partition_scheme() { return "unknown"; }
bool flash_migrate_start_16mb(const String &, bool) { return false; }
bool flash_migrate_in_progress() { return false; }
void flash_migrate_loop() {}
void flash_migrate_early_commit() {}
void flash_migrate_status_json(JsonDocument &doc) { doc["active"] = false; }
void flash_migrate_coredump_json(JsonDocument &doc) { doc["coredump"] = "n/a"; }

#endif
