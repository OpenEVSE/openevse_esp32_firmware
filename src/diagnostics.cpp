#include "diagnostics.h"

#include <Arduino.h>
#include <MongooseCore.h>
#include <MongooseHttpServer.h>

#ifdef ESP32
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

#if defined(ENABLE_SCREEN_LVGL_TFT) && (0 == LV_MEM_CUSTOM)
// Peak (not current) LVGL pool usage: the pool has to be sized for the worst
// screen transition, which a spot reading almost never catches.
static uint32_t diag_lv_used_max = 0;
static uint32_t diag_lv_frag_max = 0;
#endif

#ifdef ESP32
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
#ifdef ESP32
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

#ifdef ESP32
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

void diagnostics_status(JsonDocument &doc)
{
#ifdef ESP32
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

#if defined(ENABLE_SCREEN_LVGL_TFT) && (0 == LV_MEM_CUSTOM)
  // LV_MEM_SIZE is a fixed .bss pool. Trimming it blind is dangerous — LVGL
  // responds to pool exhaustion with an assert loop, which the task watchdog
  // turns into a reboot, so a wrong guess adds a reboot source rather than
  // removing one. Report peak usage instead and size it from measurement.
  doc["lv_used_max"] = diag_lv_used_max;
  doc["lv_frag_max"] = diag_lv_frag_max;
#endif
}
