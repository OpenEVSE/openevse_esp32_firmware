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

#include "esp_flash.h"
#include "esp_rom_crc.h"
#include "mbedtls/sha256.h"

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

static void start_download(const String &url);
static void finalize_current();
static void flash_migrate_commit();

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
    mctx.buf.insert(mctx.buf.end(), data, data + len);
  }

  mctx.dl_pos += len;
  if(mctx.dl_total > 0) {
    int pct = (int)((mctx.dl_pos * 100) / mctx.dl_total);
    if(pct > 100) {
      pct = 100;
    }
    emit_progress(pct);
  }
  feedLoopWDT();
  return true;
}

static bool finalize_app_image()
{
  // Flush the final partial sector (pad with 0xFF, region is erased anyway).
  if(mctx.sect_fill > 0)
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
      start_download(mctx.bl_url);
      break;
    }

    case ST_BOOTLOADER:
    {
      if(mctx.buf.empty() || mctx.buf[0] != 0xE9) {
        migrate_fail(-22, "bootloader magic");
        return;
      }
      if(mctx.buf.size() > BOOTLOADER_MAX) {
        migrate_fail(-27, "bootloader too large");
        return;
      }
      uint8_t dig[32];
      sha256_buf(mctx.buf.data(), mctx.buf.size(), dig);
      if(!sha_equals(dig, mctx.bl_sha)) {
        migrate_fail(-23, "bootloader sha");
        return;
      }
      mctx.bootloader.swap(mctx.buf);
      mctx.state = ST_PARTITIONS;
      start_download(mctx.pt_url);
      break;
    }

    case ST_PARTITIONS:
    {
      if(mctx.buf.size() < 96 || mctx.buf.size() > PARTITION_TABLE_MAX) {
        migrate_fail(-25, "partition table size");
        return;
      }
      uint8_t dig[32];
      sha256_buf(mctx.buf.data(), mctx.buf.size(), dig);
      if(!sha_equals(dig, mctx.pt_sha)) {
        migrate_fail(-24, "partition table sha");
        return;
      }
      mctx.partitions.swap(mctx.buf);
      emit_state("downloading_app");
      mctx.state = ST_APP;
      start_download(mctx.app_url);
      break;
    }

    case ST_APP:
    {
      emit_state("verifying");
      if(!finalize_app_image()) {
        migrate_fail(-26, "app verify");
        return;
      }
      flash_migrate_commit();
      break;
    }

    default:
      break;
  }
}

static void start_download(const String &url)
{
  DBUGF("[migrate] GET %s", url.c_str());

  // Reset per-download accumulators (also re-run on each redirect hop).
  mctx.buf.clear();
  mctx.dl_total = 0;
  mctx.dl_pos = 0;
  mctx.last_percent = -1;

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
    migrate_fail(-10, "beginRequest");
    return;
  }

  DlState *st = new DlState();
  request->setMethod(HTTP_GET);

  request->onBody([request, st](MongooseHttpClientResponse *response)
  {
    int code = response->respCode();
    if(st->finalized) {
      return;
    }
    if(code == 200)
    {
      if(mctx.dl_total == 0) {
        mctx.dl_total = response->contentLength();
      }
      if(!consume_chunk((const uint8_t *)response->body().c_str(), response->body().length())) {
        migrate_fail(-11, "flash write");
        request->abort();
        return;
      }
      // Finalize as soon as every Content-Length byte has arrived (the final
      // socket-close callback is not always delivered with the streaming client).
      if(mctx.dl_total > 0 && mctx.dl_pos >= mctx.dl_total) {
        st->finalized = true;
        finalize_current();
      }
    }
    else if(code >= 300 && code < 400)
    {
      return; // redirect handled in onResponse
    }
    else
    {
      migrate_fail(code, "http status");
      request->abort();
    }
  });

  request->onResponse([st](MongooseHttpClientResponse *response)
  {
    int code = response->respCode();
    if(301 == code || 302 == code)
    {
      st->redirected = true;
      MongooseString location = response->headers("Location");
      start_download(location.toString()); // state unchanged
    }
    else if(200 == code)
    {
      if(!st->finalized && mctx.dl_total == 0)
      {
        // No Content-Length: the whole body is in now, finalize.
        st->finalized = true;
        finalize_current();
      }
    }
  });

  request->onClose([st]()
  {
    if(mctx.active && !st->finalized && !st->redirected) {
      migrate_fail(-12, "connection closed");
    }
    delete st;
  });

  mclient.send(request);
}

// ---- commit (point of no return) ----------------------------------------

// Erase + write + read-back verify a region, with a few retries. Flash writes
// here are safe at runtime (these regions are only read by the ROM bootloader
// at boot); the only residual risk is power loss mid-write.
static bool write_and_verify(uint32_t off, const uint8_t *data, uint32_t len)
{
  uint32_t a0 = off & ~(FLASH_SECTOR - 1);
  uint32_t a1 = (off + len + FLASH_SECTOR - 1) & ~(FLASH_SECTOR - 1);

  for(int attempt = 0; attempt < 3; attempt++)
  {
    bool ok = true;
    for(uint32_t a = a0; a < a1 && ok; a += FLASH_SECTOR) {
      if(esp_flash_erase_region(esp_flash_default_chip, a, FLASH_SECTOR) != ESP_OK) {
        ok = false;
      }
      feedLoopWDT();
    }
    if(ok)
    {
      // esp_flash_write wants a 4-byte aligned length; pad with 0xFF.
      uint32_t wlen = (len + 3) & ~3u;
      if(wlen == len) {
        ok = (esp_flash_write(esp_flash_default_chip, data, off, len) == ESP_OK);
      } else {
        std::vector<uint8_t> t(data, data + len);
        t.resize(wlen, 0xFF);
        ok = (esp_flash_write(esp_flash_default_chip, t.data(), off, wlen) == ESP_OK);
      }
    }
    if(ok)
    {
      std::vector<uint8_t> rb(len);
      if(esp_flash_read(esp_flash_default_chip, rb.data(), off, len) == ESP_OK &&
         memcmp(rb.data(), data, len) == 0) {
        return true;
      }
    }
    feedLoopWDT();
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

// Point the boot selection at ota_1 (the staged 16MB firmware). With 2 OTA
// apps, boot index = (ota_seq - 1) % 2, so ota_seq=2 selects ota_1. ota_0 is
// left intact (old firmware) as a best-effort fallback slot.
static bool write_otadata_select_ota1()
{
  if(esp_flash_erase_region(esp_flash_default_chip, OTADATA_OFFSET, OTADATA_SIZE) != ESP_OK) {
    return false;
  }
  ota_select_entry_t s;
  memset(&s, 0xFF, sizeof(s));
  s.ota_seq = 2;
  s.ota_state = 0xFFFFFFFF; // ESP_OTA_IMG_UNDEFINED -> no rollback verification
  s.crc = esp_rom_crc32_le(UINT32_MAX, (const uint8_t *)&s.ota_seq, sizeof(s.ota_seq));
  if(esp_flash_write(esp_flash_default_chip, &s, OTADATA_OFFSET, sizeof(s)) != ESP_OK) {
    return false;
  }
  ota_select_entry_t rb;
  if(esp_flash_read(esp_flash_default_chip, &rb, OTADATA_OFFSET, sizeof(rb)) != ESP_OK) {
    return false;
  }
  return memcmp(&rb, &s, sizeof(s)) == 0;
}

static void flash_migrate_commit()
{
  emit_state("committing");
  DEBUG_PORT.println("[migrate] committing new 16MB layout - DO NOT POWER OFF");

  // Order chosen so that an interruption tends to still boot *something*:
  //  1) bootloader  (new core-3 boot, old table still selects old fw)
  //  2) partition table
  //  3) otadata -> ota_1  (the final "go" switch)
  if(!write_and_verify(BOOTLOADER_OFFSET, mctx.bootloader.data(), mctx.bootloader.size())) {
    migrate_fail(-30, "bootloader write");
    return;
  }
  if(!write_and_verify(PARTITION_TABLE_OFFSET, mctx.partitions.data(), mctx.partitions.size())) {
    migrate_fail(-31, "partition table write");
    return;
  }
  if(!write_otadata_select_ota1()) {
    migrate_fail(-32, "otadata write");
    return;
  }

  mctx.active = false;
  mctx.state = ST_IDLE;
  emit_state("done");
  DEBUG_PORT.println("[migrate] commit complete, rebooting into 16MB layout");
  restart_system();
}

// ---- public API ---------------------------------------------------------

bool flash_migrate_in_progress()
{
  return mctx.active;
}

bool flash_migrate_can_expand_16mb()
{
  if(mctx.active) {
    return false;
  }
  return ESP.getFlashChipSize() >= SIZE_16MB && partition_extent() <= SIZE_4MB;
}

bool flash_migrate_start_16mb(const String &manifest_url)
{
  if(mctx.active) {
    return false;
  }
  if(!flash_migrate_can_expand_16mb()) {
    return false;
  }

  mctx.app_url = ""; mctx.bl_url = ""; mctx.pt_url = "";
  mctx.app_sha = ""; mctx.bl_sha = ""; mctx.pt_sha = "";
  mctx.buf.clear();
  mctx.bootloader.clear();
  mctx.partitions.clear();
  mctx.dl_total = 0;
  mctx.dl_pos = 0;
  mctx.last_percent = -1;
  mctx.streaming_app = false;
  mctx.sect_fill = 0;
  mctx.app_seen_first = false;
  if(mctx.appsha_init) {
    mbedtls_sha256_free(&mctx.appsha);
    mctx.appsha_init = false;
  }

  mctx.active = true;
  mctx.state = ST_MANIFEST;
  mctx.manifest_url = manifest_url.length() ? manifest_url : String(MIGRATE_MANIFEST_URL);

  DEBUG_PORT.printf("[migrate] starting 16MB expand, manifest=%s\n", mctx.manifest_url.c_str());
  emit_state("staging");
  start_download(mctx.manifest_url);
  return true;
}

#else // ENABLE_FLASH_MIGRATE

// Engine not compiled in: detection still reports the scheme but expansion is
// unavailable.
bool flash_migrate_can_expand_16mb() { return false; }
bool flash_migrate_in_progress() { return false; }
bool flash_migrate_start_16mb(const String &) { return false; }

#endif // ENABLE_FLASH_MIGRATE

#else // ESP32 && !EPOXY_DUINO

// Host / non-ESP32 builds: stubs.
bool flash_migrate_can_expand_16mb() { return false; }
const char *flash_migrate_partition_scheme() { return "unknown"; }
bool flash_migrate_start_16mb(const String &) { return false; }
bool flash_migrate_in_progress() { return false; }

#endif
