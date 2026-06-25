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

#include "esp_flash.h"       // esp_flash_* — stream the 16MB app to free flash @0x650000
#include "esp_ota_ops.h"     // esp_ota_* — write the migrator into the inactive OTA slot
#include "mbedtls/sha256.h"
#include "esp_core_dump.h"

// The protected bootloader (0x1000) / partition table (0x8000) / otadata writes
// are NOT done here. The Arduino spi_flash lib is built with DANGEROUS_WRITE_
// ABORTS, so this app cannot write those regions. Instead we stage the 16MB app
// to ota_1 (0x650000) and OTA a tiny IDF "migrator" app (built with DANGEROUS_
// WRITE_ALLOWED) into the inactive 4MB OTA slot; on reboot the migrator performs
// the protected commit. See docs/flash_migrate_repartition.md and
// https://github.com/OpenEVSE/openevse-16mb-migrator

// Default release manifest. Points at this repo's GitHub releases, where the CI
// (.github/workflows/build.yaml) publishes migrate_v1_16mb.json together with
// the matching 16MB app/bootloader/partition binaries. Override with
// -D MIGRATE_MANIFEST_URL=... per build env if you publish elsewhere.
#ifndef MIGRATE_MANIFEST_URL
#define MIGRATE_MANIFEST_URL "https://github.com/OpenEVSE/openevse_esp32_firmware/releases/latest/download/migrate_v1_16mb.json"
#endif

// The 16MB app is streamed to ota_1 of the 16MB layout, which is free, unused
// flash in the current 4MB layout — so esp_flash writes it fine while WiFi runs.
static const uint32_t NEW_APP1_OFFSET       = 0x650000; // ota_1 in 16MB layout
static const uint32_t NEW_APP1_SIZE         = 0x640000;

// Manifest scratch buffer reserved up front (heap unfragmented) so the JSON
// download never reallocates mid-TLS-transfer.
static const size_t MIGRATE_SCRATCH_CAP     = 4 * 1024;
static const uint32_t MIGRATE_STALL_MS      = 25000;      // no-progress timeout

enum {
  ST_IDLE = 0,
  ST_MANIFEST,
  ST_APP,       // 16MB app -> free flash @0x650000 (raw esp_flash)
  ST_MIGRATOR,  // migrator -> inactive OTA slot (esp_ota), then boot it
};

struct MigrateCtx
{
  bool active = false;
  bool dry_run = false; // download+verify everything but write nothing / no commit
  int state = ST_IDLE;
  String manifest_url;

  String app_url, migrator_url;
  String app_sha, migrator_sha;

  std::vector<uint8_t> buf;          // buffered download (the manifest JSON)

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

  // streaming targets (one at a time): the app to raw flash @0x650000, or the
  // migrator to the inactive OTA partition via esp_ota.
  bool streaming_app = false;
  bool streaming_migrator = false;
  uint32_t app_flash_off = 0;
  uint8_t sect[FLASH_SECTOR];
  uint32_t sect_fill = 0;
  bool seen_first = false;
  uint8_t first_byte = 0;            // app image magic check (0xE9)

  esp_ota_handle_t ota_handle = 0;   // migrator OTA write handle
  const esp_partition_t *ota_part = NULL;

  mbedtls_sha256_context dlsha;      // SHA-256 of the current streamed image
  bool dlsha_init = false;
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

static void start_download(const String &url);
static void finalize_current();

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
  mctx.streaming_migrator = false;
  if(mctx.dlsha_init) {
    mbedtls_sha256_free(&mctx.dlsha);
    mctx.dlsha_init = false;
  }
  if(mctx.ota_handle) {
    esp_ota_abort(mctx.ota_handle);
    mctx.ota_handle = 0;
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

static bool consume_chunk(const uint8_t *data, size_t len)
{
  if(mctx.streaming_app || mctx.streaming_migrator)
  {
    if(len) {
      mbedtls_sha256_update_ret(&mctx.dlsha, data, len);
    }
    if(!mctx.seen_first && len > 0) {
      mctx.first_byte = data[0];
      mctx.seen_first = true;
    }
  }

  if(mctx.streaming_app)
  {
    if(!mctx.dry_run) {
      // Stream into the free flash at ota_1 (0x650000), one sector at a time.
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
  }
  else if(mctx.streaming_migrator)
  {
    // Stream into the inactive OTA partition via esp_ota (handles erase).
    if(!mctx.dry_run && len) {
      if(esp_ota_write(mctx.ota_handle, data, len) != ESP_OK) {
        return false;
      }
    }
  }
  else
  {
    // Buffered (the manifest JSON). Never reallocate mid-TLS: growing the vector
    // fragments the heap and can throw bad_alloc -> panic. Reserved up front.
    if(mctx.buf.size() + len > mctx.buf.capacity()) {
      return false;
    }
    mctx.buf.insert(mctx.buf.end(), data, data + len);
  }

  mctx.dl_pos += len;
  feedLoopWDT();
  return true;
}

// Finish + verify the currently-streamed image (app or migrator).
static bool finalize_streamed_image(const String &expected_sha, bool check_app_magic)
{
  // App only: flush the final partial sector (padded with 0xFF).
  if(mctx.streaming_app && mctx.sect_fill > 0 && !mctx.dry_run)
  {
    memset(mctx.sect + mctx.sect_fill, 0xFF, FLASH_SECTOR - mctx.sect_fill);
    if(!flash_write_sector(mctx.app_flash_off, mctx.sect, FLASH_SECTOR)) {
      return false;
    }
    mctx.app_flash_off += FLASH_SECTOR;
    mctx.sect_fill = 0;
  }

  uint8_t dig[32];
  mbedtls_sha256_finish_ret(&mctx.dlsha, dig);
  mbedtls_sha256_free(&mctx.dlsha);
  mctx.dlsha_init = false;

  if(check_app_magic && mctx.first_byte != 0xE9) {
    DEBUG_PORT.println("[migrate] image bad magic");
    return false;
  }
  if(!sha_equals(dig, expected_sha)) {
    DEBUG_PORT.println("[migrate] image sha mismatch");
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
      mctx.app_url      = (const char *)(doc["app"]["url"]            | "");
      mctx.app_sha      = (const char *)(doc["app"]["sha256"]         | "");
      mctx.migrator_url = (const char *)(doc["migrator"]["url"]       | "");
      mctx.migrator_sha = (const char *)(doc["migrator"]["sha256"]    | "");
      if(mctx.app_url.length() == 0 || mctx.migrator_url.length() == 0 ||
         mctx.app_sha.length() != 64 || mctx.migrator_sha.length() != 64) {
        migrate_fail(-21, "manifest fields");
        return;
      }
      emit_state("downloading_app");
      mctx.state = ST_APP;
      schedule_download(mctx.app_url);
      break;
    }

    case ST_APP:
    {
      // 16MB app streamed to ota_1 (0x650000); verify before staging the migrator.
      emit_state("verifying");
      if(!finalize_streamed_image(mctx.app_sha, /*check_app_magic=*/true)) {
        migrate_fail(-26, "app verify");
        return;
      }
      emit_state("downloading_migrator");
      mctx.state = ST_MIGRATOR;
      schedule_download(mctx.migrator_url);
      break;
    }

    case ST_MIGRATOR:
    {
      emit_state("verifying");
      if(mctx.dry_run) {
        // download+verify only; never write flash / OTA / boot.
        if(!finalize_streamed_image(mctx.migrator_sha, /*check_app_magic=*/false)) {
          migrate_fail(-26, "migrator verify");
          return;
        }
        DEBUG_PORT.println("[migrate] dry run OK - app + migrator verified, nothing written");
        mctx.active = false;
        mctx.state = ST_IDLE;
        emit_state("dryrun_ok");
        return;
      }
      // Finalize the OTA image (esp_ota_end validates magic/checksum) then verify
      // our own SHA-256 before we let it become bootable.
      esp_err_t e = esp_ota_end(mctx.ota_handle);
      mctx.ota_handle = 0;
      if(e != ESP_OK) {
        migrate_fail(-28, "migrator esp_ota_end");
        return;
      }
      if(!finalize_streamed_image(mctx.migrator_sha, /*check_app_magic=*/false)) {
        migrate_fail(-26, "migrator verify");
        return;
      }
      if(esp_ota_set_boot_partition(mctx.ota_part) != ESP_OK) {
        migrate_fail(-29, "set boot partition");
        return;
      }
      mctx.active = false;
      mctx.state = ST_IDLE;
      emit_state("done");
      DEBUG_PORT.println("[migrate] migrator staged + selected; rebooting to commit 16MB layout");
      restart_system();
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

  mctx.buf.clear(); // manifest scratch (kept capacity)
  mctx.dl_total = 0;
  mctx.dl_pos = 0;
  mctx.last_dl_pos = 0;
  mctx.last_percent = -1;
  mctx.body_complete = false;
  mctx.has_redirect = false;
  mctx.last_progress_ms = millis();
  mctx.streaming_app = false;
  mctx.streaming_migrator = false;

  if(mctx.state == ST_APP || mctx.state == ST_MIGRATOR)
  {
    // Reset the streaming SHA + first-byte (re-run on each redirect hop).
    mctx.seen_first = false;
    mctx.first_byte = 0;
    if(mctx.dlsha_init) {
      mbedtls_sha256_free(&mctx.dlsha);
    }
    mbedtls_sha256_init(&mctx.dlsha);
    mbedtls_sha256_starts_ret(&mctx.dlsha, 0);
    mctx.dlsha_init = true;

    if(mctx.state == ST_APP) {
      mctx.streaming_app = true;
      mctx.app_flash_off = NEW_APP1_OFFSET;
      mctx.sect_fill = 0;
    } else {
      mctx.streaming_migrator = true;
      if(!mctx.dry_run) {
        // (Re)open the inactive OTA partition for the migrator write.
        if(mctx.ota_handle) {
          esp_ota_abort(mctx.ota_handle);
          mctx.ota_handle = 0;
        }
        mctx.ota_part = esp_ota_get_next_update_partition(NULL);
        if(!mctx.ota_part ||
           esp_ota_begin(mctx.ota_part, OTA_SIZE_UNKNOWN, &mctx.ota_handle) != ESP_OK) {
          flag_fail(-27, "esp_ota_begin");
          return;
        }
      }
    }
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

// ---- public API ---------------------------------------------------------

bool flash_migrate_in_progress()
{
  return mctx.active;
}

void flash_migrate_status_json(JsonDocument &doc)
{
  static const char *names[] = { "idle", "manifest", "app", "migrator" };
  doc["active"] = mctx.active;
  doc["state"] = (mctx.state >= 0 && mctx.state <= ST_MIGRATOR) ? names[mctx.state] : "?";
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

  mctx.app_url = ""; mctx.app_sha = "";
  mctx.migrator_url = ""; mctx.migrator_sha = "";
  mctx.buf.clear();
  // Reserve the manifest buffer now, while the heap is unfragmented. Doing this
  // lazily during a TLS download throws bad_alloc -> panic (the heap is
  // fragmented by the active mbedTLS connection). bad_alloc is caught so a
  // genuinely full heap fails cleanly instead of crashing.
  try {
    mctx.buf.reserve(MIGRATE_SCRATCH_CAP);
  } catch(...) {
    DEBUG_PORT.println("[migrate] could not reserve manifest buffer");
    return false;
  }
  mctx.dl_total = 0;
  mctx.dl_pos = 0;
  mctx.last_percent = -1;
  mctx.streaming_app = false;
  mctx.streaming_migrator = false;
  mctx.sect_fill = 0;
  mctx.seen_first = false;
  mctx.conn_open = false;
  mctx.awaiting_result = false;
  mctx.body_complete = false;
  mctx.has_redirect = false;
  mctx.failed = false;
  mctx.has_pending = false;
  mctx.pending_url = "";
  if(mctx.dlsha_init) {
    mbedtls_sha256_free(&mctx.dlsha);
    mctx.dlsha_init = false;
  }
  if(mctx.ota_handle) {
    esp_ota_abort(mctx.ota_handle);
    mctx.ota_handle = 0;
  }

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
void flash_migrate_status_json(JsonDocument &doc) { doc["active"] = false; }
void flash_migrate_coredump_json(JsonDocument &doc) { doc["coredump"] = "n/a"; }

#endif
