// src/lvgl_tft/lvgl_panel.cpp — see lvgl_panel.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>

#if defined(EPOXY_DUINO)
#include <stdlib.h>
#else
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#endif

#include "lvgl_panel.h"

// The ILI9488 forces TFT_eSPI's SPI_18BIT_DRIVER, which disables ESP32_DMA
// (DMA only supports 16-bit pushes). So there is no DMA path on this panel —
// the flush always uses blocking pushPixels.
#if defined(ESP32_DMA)
#warning "ESP32_DMA unexpectedly available on ILI9488 — still using blocking pushPixels"
#endif

// Landscape: native panel is 320x480, rotated to 480x320.
static const uint16_t SCREEN_W = TFT_HEIGHT; // 480
static const uint16_t SCREEN_H = TFT_WIDTH;  // 320

// ONE partial buffer (~1/10 screen) in INTERNAL DRAM — this board has no PSRAM.
// A single buffer is correct: no DMA means flush_cb blocks the CPU, so a second
// buffer could never overlap a flush.
static const uint32_t DRAW_BUF_PIXELS = SCREEN_W * 32; // 480*32 = 15360 px (~30 KB)

#if !defined(EPOXY_DUINO)
static TFT_eSPI tft = TFT_eSPI();
#endif

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_color_t *buf1 = nullptr;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
#if defined(EPOXY_DUINO)
  (void)area;
  (void)color_p;
  lv_disp_flush_ready(drv);
#else
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels((uint16_t *)&color_p->full, w * h);
  tft.endWrite();

  lv_disp_flush_ready(drv);
#endif
}

bool lvgl_panel_begin()
{
#if defined(EPOXY_DUINO)
  lv_init();

  const size_t buf_bytes = DRAW_BUF_PIXELS * sizeof(lv_color_t);
  buf1 = (lv_color_t *)malloc(buf_bytes);
  if (buf1 == nullptr) {
    Serial.printf("[panel] FATAL: draw-buffer alloc failed (%u B host heap)\n",
                  (unsigned)buf_bytes);
    return false;
  }
#else
  tft.init();
  tft.setRotation(1); // landscape, matches the original renderer
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  lv_init();

  const size_t buf_bytes = DRAW_BUF_PIXELS * sizeof(lv_color_t);
  buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf1 == nullptr) {
    Serial.printf("[panel] FATAL: draw-buffer alloc failed (%u B internal); largest free block=%u\n",
                  (unsigned)buf_bytes,
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return false;
  }
#endif
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, DRAW_BUF_PIXELS);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

#if defined(EPOXY_DUINO)
  Serial.printf("[panel] headless LVGL display up %ux%u, 1 buf %u B host heap\n",
                SCREEN_W, SCREEN_H, (unsigned)buf_bytes);
#else
  Serial.printf("[panel] display up %ux%u, 1 buf %u B internal, free internal heap=%u\n",
                SCREEN_W, SCREEN_H, (unsigned)buf_bytes,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
  return true;
}

#endif // ENABLE_SCREEN_LVGL_TFT
