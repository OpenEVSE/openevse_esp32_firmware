#include "diagnostics.h"

#include <Arduino.h>
#include <MongooseCore.h>
#include <MongooseHttpServer.h>

// native_openevse defines -D ESP32 for the host build (platformio.ini), so a
// bare "#ifdef ESP32" is true there too and pulls in IDF headers that do not
// exist on the host. Gate on the real condition: an ESP32 target that is not
// the EpoxyDuino host.
#if defined(ESP32) && !defined(EPOXY_DUINO)
#define DIAG_HAVE_IDF 1
#else
#define DIAG_HAVE_IDF 0
#endif

#if DIAG_HAVE_IDF
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// esp_core_dump_image_get()/erase() are always available; the decoded summary
// exists only for the ELF dump format.
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
#define DIAG_COREDUMP_SUMMARY 1
#else
#define DIAG_COREDUMP_SUMMARY 0
#endif

// esp_core_dump_get_panic_reason() arrived in IDF 5. On the core-2.x (IDF 4.4)
// boards the summary still decodes; only the human-readable reason string is
// missing, so that one field is simply absent from the response there.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define DIAG_COREDUMP_PANIC_REASON 1
#else
#define DIAG_COREDUMP_PANIC_REASON 0
#endif

// esp_partition_mmap() took a spi_flash_mmap_handle_t and SPI_FLASH_MMAP_DATA
// before IDF 5 renamed both into the esp_partition namespace.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
typedef esp_partition_mmap_handle_t diag_mmap_handle_t;
#define DIAG_MMAP_DATA ESP_PARTITION_MMAP_DATA
#else
typedef spi_flash_mmap_handle_t diag_mmap_handle_t;
#define DIAG_MMAP_DATA SPI_FLASH_MMAP_DATA
#endif
#endif

#ifdef ENABLE_SCREEN_LVGL_TFT
#include <lvgl.h>
#endif

// Outbound websocket buffer above which a client is considered stalled and
// dropped. Sized well under the free heap this board runs with (~60KB) so that
// even several stalled clients cannot exhaust it between reaps.
#ifndef DIAG_WS_SEND_LIMIT
#define DIAG_WS_SEND_LIMIT 4096
#endif

// The transients worth catching are short. Sampling every 5s missed dips that
// a request burst opens and closes well inside one interval, so sample often;
// heap_caps_get_largest_free_block walks only the free list and is cheap
// enough at this rate.
#define DIAG_SAMPLE_INTERVAL 1000

static uint32_t diag_reset_reason = 0;
static uint32_t diag_last_sample = 0;

// Low-water marks: the smallest value seen since boot. The current reading
// tells you little on its own because a sample taken between two bursts looks
// healthy; the minimum is what the device actually survived.
static uint32_t diag_largest_block_min = UINT32_MAX;
static uint32_t diag_stack_loop_min = UINT32_MAX;
static uint32_t diag_stack_events_min = UINT32_MAX;

// Websocket pressure.
static uint32_t diag_ws_send_max = 0;
static uint32_t diag_ws_reaped = 0;
static uint32_t diag_ws_conns = 0;

static uint32_t diag_probe_max[DIAG_PROBE_SLOTS] = {0};
static uint32_t diag_probe_hits[DIAG_PROBE_SLOTS] = {0};

#if DIAG_HAVE_IDF
// Flash mapping of the core dump partition. Created at most once per boot and
// never torn down: the HTTP response that hands this pointer out is sent
// asynchronously, so unmapping on any other schedule is a use-after-free
// waiting for a slow client. esp_core_dump_image_get() -- not this pointer --
// is the authority on whether a dump currently exists, so an erase does not
// need to invalidate the mapping, and a *new* dump cannot appear without a
// reboot clearing these statics anyway.
static const void *diag_cd_map = NULL;
static diag_mmap_handle_t diag_cd_handle = 0;
static size_t diag_cd_len = 0;
#endif

#if defined(ENABLE_SCREEN_LVGL_TFT) && (0 == LV_MEM_CUSTOM)
// Peak (not current) LVGL pool usage: the pool has to be sized for the worst
// screen transition, which a spot reading almost never catches.
static uint32_t diag_lv_used_max = 0;
static uint32_t diag_lv_frag_max = 0;
#endif

#if DIAG_HAVE_IDF
static const char *diagnostics_reset_reason_name(uint32_t reason)
{
  switch(reason)
  {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_EXT:      return "external";
    case ESP_RST_SW:       return "sw";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_DEEPSLEEP:return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
  }
}
#endif

void diagnostics_begin()
{
#if DIAG_HAVE_IDF
  diag_reset_reason = (uint32_t)esp_reset_reason();
#endif
}

void diagnostics_loop()
{
  uint32_t now = millis();
  if(0 != diag_last_sample && now - diag_last_sample < DIAG_SAMPLE_INTERVAL) {
    return;
  }
  diag_last_sample = now;

#if DIAG_HAVE_IDF
  uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if(largest < diag_largest_block_min) {
    diag_largest_block_min = largest;
  }

  // uxTaskGetStackHighWaterMark reports the smallest free stack ever seen for
  // the task, in words, so it is already a minimum; tracking our own minimum
  // costs nothing and keeps the two metrics symmetric.
  uint32_t loop_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
  if(loop_free < diag_stack_loop_min) {
    diag_stack_loop_min = loop_free;
  }

#if defined(ENABLE_SCREEN_LVGL_TFT) && (0 == LV_MEM_CUSTOM)
  // Must be guarded: this runs from loop() ahead of MicroTask.update(), and
  // MicroTask.update() is what first runs LcdTask::setup() -> lv_init(). On
  // the first pass after boot the pool does not exist yet, and lv_mem_monitor
  // walks it unconditionally -- an unguarded call panics with LoadProhibited
  // and boot-loops the board.
  if(lv_is_initialized())
  {
    lv_mem_monitor_t lv_mon;
    lv_mem_monitor(&lv_mon);
    if(lv_mon.used_pct > diag_lv_used_max) {
      diag_lv_used_max = lv_mon.used_pct;
    }
    if(lv_mon.frag_pct > diag_lv_frag_max) {
      diag_lv_frag_max = lv_mon.frag_pct;
    }
  }
#endif

  TaskHandle_t events = xTaskGetHandle("arduino_events");
  if(nullptr != events) {
    uint32_t events_free = (uint32_t)uxTaskGetStackHighWaterMark(events) * sizeof(StackType_t);
    if(events_free < diag_stack_events_min) {
      diag_stack_events_min = events_free;
    }
  }
#endif
}

int diagnostics_ws_reap()
{
  int reaped = 0;
  uint32_t conns = 0;

  mg_mgr *mgr = Mongoose.getMgr();
  if(nullptr == mgr) {
    return 0;
  }

  struct mg_connection *next = nullptr;
  for(struct mg_connection *c = mg_next(mgr, nullptr); nullptr != c; c = next)
  {
    // Take the next pointer before we flag this one for close, so that
    // dropping a connection cannot strand the walk.
    next = mg_next(mgr, c);

    if(!(c->flags & MG_F_IS_WEBSOCKET)) {
      continue;
    }
    conns++;

    uint32_t pending = (uint32_t)c->send_mbuf.len;
    if(pending > diag_ws_send_max) {
      diag_ws_send_max = pending;
    }

    if(pending > DIAG_WS_SEND_LIMIT) {
      c->flags |= MG_F_CLOSE_IMMEDIATELY;
      diag_ws_reaped++;
      reaped++;
    }
  }

  diag_ws_conns = conns;
  return reaped;
}

uint32_t diagnostics_probe_begin()
{
#if DIAG_HAVE_IDF
  return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
  return 0;
#endif
}

void diagnostics_probe_end(int slot, uint32_t start)
{
#if DIAG_HAVE_IDF
  if(slot < 0 || slot >= DIAG_PROBE_SLOTS) {
    return;
  }
  uint32_t now = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  diag_probe_hits[slot]++;
  if(now < start) {
    uint32_t drop = start - now;
    if(drop > diag_probe_max[slot]) {
      diag_probe_max[slot] = drop;
    }
  }
#endif
}

void diagnostics_status(JsonDocument &doc)
{
#if DIAG_HAVE_IDF
  // Fold this reading into the minimum as well. /status is itself one of the
  // heavier allocations, so a sample taken here is a sample taken under load —
  // exactly the moment the periodic sampler is least likely to have caught.
  uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if(largest < diag_largest_block_min) {
    diag_largest_block_min = largest;
  }

  doc["heap_largest"] = largest;
  doc["heap_largest_min"] = UINT32_MAX == diag_largest_block_min ? 0 : diag_largest_block_min;
  doc["heap_min"] = (uint32_t)esp_get_minimum_free_heap_size();
  doc["reset_reason"] = diag_reset_reason;
  doc["reset_reason_name"] = diagnostics_reset_reason_name(diag_reset_reason);
  doc["stack_loop_min"] = UINT32_MAX == diag_stack_loop_min ? 0 : diag_stack_loop_min;
  doc["stack_events_min"] = UINT32_MAX == diag_stack_events_min ? 0 : diag_stack_events_min;
#endif
  doc["ws_conns"] = diag_ws_conns;
  doc["ws_send_max"] = diag_ws_send_max;
  doc["ws_reaped"] = diag_ws_reaped;
  for(int i = 0; i < DIAG_PROBE_SLOTS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "probe%d_max", i);
    doc[key] = diag_probe_max[i];
    snprintf(key, sizeof(key), "probe%d_n", i);
    doc[key] = diag_probe_hits[i];
  }

#if defined(ENABLE_SCREEN_LVGL_TFT) && (0 == LV_MEM_CUSTOM)
  // LV_MEM_SIZE is a fixed .bss pool. Trimming it blind is dangerous — LVGL
  // responds to pool exhaustion with an assert loop, which the task watchdog
  // turns into a reboot, so a wrong guess adds a reboot source rather than
  // removing one. Report peak usage instead and size it from measurement.
  doc["lv_used_max"] = diag_lv_used_max;
  doc["lv_frag_max"] = diag_lv_frag_max;
#endif
}

void diagnostics_coredump_json(JsonDocument &doc)
{
#if DIAG_HAVE_IDF
  size_t addr = 0, size = 0;
  if(ESP_OK != esp_core_dump_image_get(&addr, &size) || 0 == size) {
    doc["present"] = false;
    return;
  }

  doc["present"] = true;
  doc["size"] = (uint32_t)size;
  doc["addr"] = (uint32_t)addr;

  // Verifies the stored checksum. A dump truncated by a second reset landing
  // mid-write is reported as invalid rather than quietly decoded into
  // plausible nonsense.
  esp_err_t chk = esp_core_dump_image_check();
  doc["valid"] = (ESP_OK == chk);
  if(ESP_OK != chk) {
    doc["check_err"] = (int)chk;
  }

#if DIAG_COREDUMP_SUMMARY
#if DIAG_COREDUMP_PANIC_REASON
  char reason[128];
  if(ESP_OK == esp_core_dump_get_panic_reason(reason, sizeof(reason))) {
    doc["panic_reason"] = reason;
  }
#endif

  // Roughly 2KB of registers and backtrace -- more than the loop task's stack
  // headroom, so it goes on the heap and is released before the document is
  // serialised.
  esp_core_dump_summary_t *s =
      (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if(!s) {
    doc["summary_err"] = "nomem";
    return;
  }

  if(ESP_OK == esp_core_dump_get_summary(s))
  {
    char buf[12];
    // Every value below is handed to ArduinoJson as a non-const char *, which
    // it copies into the document. A const char * would be stored by pointer
    // and dangle the moment this buffer goes out of scope or `s` is freed.
    doc["task"] = s->exc_task;
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->exc_pc);
    doc["pc"] = buf;
    // The exception detail and the backtrace are architecture specific: the
    // Xtensa parts carry an EXCCAUSE/EXCVADDR pair and a walked backtrace,
    // the RISC-V parts (C3, and the C6 co-processor) carry the machine trap
    // CSRs and no backtrace at all -- RISC-V cannot unwind on device without
    // parsing DWARF, so the IDF stores a raw stack dump for the host to walk.
#if defined(__riscv)
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->ex_info.mcause);
    doc["mcause"] = buf;
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->ex_info.mtval);
    doc["mtval"] = buf;
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->ex_info.ra);
    doc["ra"] = buf;
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->ex_info.sp);
    doc["sp"] = buf;
#else
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->ex_info.exc_cause);
    doc["exc_cause"] = buf;
    snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->ex_info.exc_vaddr);
    doc["exc_vaddr"] = buf;
#endif

    // Identifies the exact build that crashed, so a backtrace is never
    // symbolised against the wrong ELF.
    doc["elf_sha256"] = (char *)s->app_elf_sha256;

#if defined(__riscv)
    // No on-device backtrace to offer. Say so explicitly rather than
    // returning an empty `bt` that reads as "the stack was clean"; the raw
    // image from /debug/crash/raw still carries the stack dump.
    doc["bt"] = "riscv-no-unwind";
#else
    JsonArray bt = doc.createNestedArray("bt");
    for(uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
      snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s->exc_bt_info.bt[i]);
      bt.add(buf);
    }
    doc["bt_corrupted"] = s->exc_bt_info.corrupted;
#endif
  }
  else
  {
    doc["summary_err"] = "decode";
  }

  free(s);
#endif // DIAG_COREDUMP_SUMMARY
#else
  doc["present"] = false;
#endif // DIAG_HAVE_IDF
}

bool diagnostics_coredump_image(const uint8_t **data, size_t *len)
{
#if DIAG_HAVE_IDF
  size_t addr = 0, size = 0;
  if(ESP_OK != esp_core_dump_image_get(&addr, &size) || 0 == size) {
    return false;
  }

  if(diag_cd_map && diag_cd_len == size) {
    *data = (const uint8_t *)diag_cd_map;
    *len = diag_cd_len;
    return true;
  }

  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
  if(!part || addr < part->address || (addr - part->address) + size > part->size) {
    return false;
  }

  // Map rather than copy. The image runs to the partition size (64KB on this
  // layout) and the largest free block on this board sits near 20KB under
  // load, so a buffered copy would fail precisely when a crash report is most
  // wanted.
  const void *ptr = NULL;
  diag_mmap_handle_t handle = 0;
  if(ESP_OK != esp_partition_mmap(part, addr - part->address, size,
                                  DIAG_MMAP_DATA, &ptr, &handle)) {
    return false;
  }

  diag_cd_map = ptr;
  diag_cd_handle = handle;
  diag_cd_len = size;

  *data = (const uint8_t *)ptr;
  *len = size;
  return true;
#else
  (void)data;
  (void)len;
  return false;
#endif
}

bool diagnostics_coredump_erase()
{
#if DIAG_HAVE_IDF
  return ESP_OK == esp_core_dump_image_erase();
#else
  return false;
#endif
}
