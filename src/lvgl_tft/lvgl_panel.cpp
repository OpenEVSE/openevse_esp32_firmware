// src/lvgl_tft/lvgl_panel.cpp — see lvgl_panel.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

#include "lvgl_panel.h"
#include "backlight.h"

// The ILI9488 forces TFT_eSPI's SPI_18BIT_DRIVER, which disables ESP32_DMA
// (DMA only supports 16-bit pushes). So there is no DMA path on this panel —
// the flush always uses blocking pushPixels.
#if defined(ESP32_DMA)
#warning "ESP32_DMA unexpectedly available on ILI9488 — still using blocking pushPixels"
#endif

// Backlight PWM. RGB-LED PWM (LedManagerTask) uses LEDC channels 1..3 and WS2812
// uses RMT, so channel 0 is free for the 2.x ledcAttachPin path.
#ifndef LCD_BL_PWM_FREQ
#define LCD_BL_PWM_FREQ 5000
#endif
#ifndef LCD_BL_PWM_RES
#define LCD_BL_PWM_RES 8
#endif
#ifndef LCD_BL_LEDC_CHANNEL
#define LCD_BL_LEDC_CHANNEL 0
#endif

static bool bl_ready = false;

// Landscape: native panel is 320x480, rotated to 480x320.
static const uint16_t SCREEN_W = TFT_HEIGHT; // 480
static const uint16_t SCREEN_H = TFT_WIDTH;  // 320

// ONE partial buffer (~1/10 screen) in INTERNAL DRAM — this board has no PSRAM.
// A single buffer is correct: no DMA means flush_cb blocks the CPU, so a second
// buffer could never overlap a flush.
static const uint32_t DRAW_BUF_PIXELS = SCREEN_W * 32; // 480*32 = 15360 px (~30 KB)

static TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_color_t *buf1 = nullptr;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels((uint16_t *)&color_p->full, w * h);
  tft.endWrite();

  lv_disp_flush_ready(drv);
}

bool lvgl_panel_begin()
{
  tft.init();
  tft.setRotation(1); // landscape, matches the original renderer
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(TFT_BL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
#else
  ledcSetup(LCD_BL_LEDC_CHANNEL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
  ledcAttachPin(TFT_BL, LCD_BL_LEDC_CHANNEL);
#endif
  bl_ready = true;
  lvgl_panel_set_backlight(100); // full on until LcdTask applies the configured level

#ifdef LCD_BL_PWM_SELFTEST
  // Gated diagnostic: ramp the backlight up/down a few times so a human can
  // confirm the panel actually dims (some BL circuits are on/off-only).
  for (int cycle = 0; cycle < 3; ++cycle) {
    for (int p = 0; p <= 100; p += 5) { lvgl_panel_set_backlight((uint8_t)p); delay(30); }
    for (int p = 100; p >= 0; p -= 5) { lvgl_panel_set_backlight((uint8_t)p); delay(30); }
  }
  lvgl_panel_set_backlight(100);
#endif

  lv_init();

  const size_t buf_bytes = DRAW_BUF_PIXELS * sizeof(lv_color_t);
  buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf1 == nullptr) {
    Serial.printf("[panel] FATAL: draw-buffer alloc failed (%u B internal); largest free block=%u\n",
                  (unsigned)buf_bytes,
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return false;
  }
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, DRAW_BUF_PIXELS);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  Serial.printf("[panel] display up %ux%u, 1 buf %u B internal, free internal heap=%u\n",
                SCREEN_W, SCREEN_H, (unsigned)buf_bytes,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return true;
}

void lvgl_panel_set_backlight(uint8_t pct)
{
  if (!bl_ready) {
    return;
  }
  uint8_t duty = bl_pct_to_duty(pct);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(TFT_BL, duty);
#else
  ledcWrite(LCD_BL_LEDC_CHANNEL, duty);
#endif
}

#endif // ENABLE_SCREEN_LVGL_TFT
