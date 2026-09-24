// src/lvgl_tft/lvgl_panel.cpp — see lvgl_panel.h.
#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <lvgl.h>

#if defined(EPOXY_DUINO)
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define LVGL_PANEL_HAS_SDL 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define LVGL_PANEL_HAS_SDL 1
#else
#define LVGL_PANEL_HAS_SDL 0
#endif
#else
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#endif

#include "lvgl_panel.h"
#include "backlight.h"

// The ILI9488 forces TFT_eSPI's SPI_18BIT_DRIVER, which disables ESP32_DMA
// (DMA only supports 16-bit pushes). So there is no DMA path on this panel —
// the flush always uses blocking pushPixels.
#if defined(ESP32_DMA)
#warning "ESP32_DMA unexpectedly available on ILI9488 — still using blocking pushPixels"
#endif

// Backlight PWM. RGB-LED PWM (LedManagerTask) uses LEDC channels 1..3 and WS2812
// uses RMT, so channel 0 is free (relevant only to the 2.x ledcAttachPin path;
// core 3.x's ledcAttach allocates a channel itself).
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

// ONE partial buffer in INTERNAL DRAM.
//
// 16 lines on the stock board: at 32 this took a 30KB contiguous block at boot
// out of a heap with only ~60KB free; instrumentation on hardware showed the
// largest allocatable block down at 11KB while total free sat flat at ~60KB.
// Halving costs twice as many flush calls for the same total pixels -- small
// next to the blocking SPI write itself -- and returns 15KB of contiguous DRAM.
// openevse_s3_lcd overrides DRAW_BUF_LINES=32 from its env (its internal heap is
// not under the same pressure because the network stack lives in PSRAM).
//
// Single-buffered because of the TFT_eSPI boundary noted above, not because of
// anything about the ILI9488 or the S3: SPI_18BIT_DRIVER compiles the library's
// whole DMA subsystem out (Processors/TFT_eSPI_ESP32.h -- ESP32_DMA is only defined
// `#if !defined(TFT_PARALLEL_8_BIT) && !defined(SPI_18BIT_DRIVER)`), and
// pushPixelsDMA() is hardwired to `trans.length = len * 16` regardless, while this
// panel's path writes 3 bytes/pixel. DMA here would clock garbage into the panel.
// So flush_cb blocks the CPU, and a second buffer could never overlap a flush.
//
// Internal DRAM, and on PSRAM boards (openevse_s3_lcd) that is enforced rather than
// assumed -- see the note at the heap_caps_malloc() call. PSRAM is not idle on those
// boards; mbedTLS and the LVGL object pool are routed there. What it must not hold is
// this buffer, which the CPU reads. docs/hardware/esp32-s3-lcd.md has the detail.
//
// This also fixes the wire format: 18 bpp, with a CPU-side RGB565->RGB666 conversion
// on every pixel. If the display link ever becomes the bottleneck, the fix is to port
// this layer to esp_lcd (esp_lcd_ili9488 does the conversion AND DMA), not to patch
// TFT_eSPI -- dmaHAL is private and initDMA() is compiled out, so it cannot be done
// from the app side.
//
// !! THIS FILE IS SHARED WITH SHIPPED HARDWARE. !!
// openevse_wifi_tft_v1 and openevse_s3_lcd both pull in lvgl_tft_renderer_flags;
// there is no separate S3 panel layer. The constraint above is identical on both
// (SPI_18BIT_DRIVER follows ILI9488_DRIVER, not the chip), so a DMA rework would pay
// off on the stock board too -- but it must be conditioned on the board and proven on
// the S3 first: the stock board has no PSRAM to stage a second buffer in, only ~320 KB
// of internal heap already shared with WiFi and TLS, and it is in the field.
// Treat a clock bump separately from the DMA rework. 80 MHz is available on the stock
// board in principle (its TFT pins are the ESP32-classic HSPI IO_MUX set, so it also
// bypasses the GPIO matrix), but that board is the QD354801 direct-solder part while
// the S3 is an ER-TFT035-6 on FPC through a ZIF -- different trace lengths, different
// flex path. A clean 80 MHz result on one is not evidence for the other, and on
// shipped units there is no series termination to add and nothing to recall.
#ifndef DRAW_BUF_LINES
#define DRAW_BUF_LINES 16
#endif
static const uint32_t DRAW_BUF_PIXELS = SCREEN_W * DRAW_BUF_LINES; // 480*16 = 7680 px (~15 KB)

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_color_t *buf1 = nullptr;

#if defined(EPOXY_DUINO)
static lv_color_t *host_fb = nullptr;
static LvglPanelDisplayMode display_mode = LVGL_PANEL_DISPLAY_HEADLESS;

#if LVGL_PANEL_HAS_SDL
struct SdlApi {
  void *handle = nullptr;
  int (*Init)(uint32_t flags) = nullptr;
  void (*Quit)(void) = nullptr;
  const char *(*GetError)(void) = nullptr;
  SDL_Window *(*CreateWindow)(const char *title, int x, int y, int w, int h, uint32_t flags) = nullptr;
  SDL_Renderer *(*CreateRenderer)(SDL_Window *window, int index, uint32_t flags) = nullptr;
  SDL_Texture *(*CreateTexture)(SDL_Renderer *renderer, uint32_t format, int access, int w, int h) = nullptr;
  void (*DestroyTexture)(SDL_Texture *texture) = nullptr;
  void (*DestroyRenderer)(SDL_Renderer *renderer) = nullptr;
  void (*DestroyWindow)(SDL_Window *window) = nullptr;
  int (*UpdateTexture)(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) = nullptr;
  int (*RenderClear)(SDL_Renderer *renderer) = nullptr;
  int (*RenderCopy)(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect) = nullptr;
  void (*RenderPresent)(SDL_Renderer *renderer) = nullptr;
  int (*PollEvent)(SDL_Event *event) = nullptr;
};

struct SdlWindowState {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  bool dirty = false;
};

static SdlApi sdl;
static SdlWindowState sdl_window;
#endif

static void lvgl_panel_cleanup_host_allocations()
{
  if(host_fb != nullptr) {
    free(host_fb);
    host_fb = nullptr;
  }
  if(buf1 != nullptr) {
    free(buf1);
    buf1 = nullptr;
  }
}

#if LVGL_PANEL_HAS_SDL
static void unload_sdl()
{
  if(sdl_window.texture && sdl.DestroyTexture) {
    sdl.DestroyTexture(sdl_window.texture);
  }
  if(sdl_window.renderer && sdl.DestroyRenderer) {
    sdl.DestroyRenderer(sdl_window.renderer);
  }
  if(sdl_window.window && sdl.DestroyWindow) {
    sdl.DestroyWindow(sdl_window.window);
  }
  if(sdl.Quit) {
    sdl.Quit();
  }
  if(sdl.handle) {
    dlclose(sdl.handle);
  }

  sdl_window = {};
  sdl = {};
}

static bool load_symbol(void **target, const char *name)
{
  *target = dlsym(sdl.handle, name);
  return *target != nullptr;
}

static bool load_sdl()
{
  if(sdl.handle != nullptr) {
    return true;
  }

  static const char *const names[] = {
#if defined(_WIN32)
    "SDL2.dll",
#elif defined(__APPLE__)
    "libSDL2.dylib",
    "SDL2.framework/SDL2",
#else
    "libSDL2-2.0.so.0",
    "libSDL2.so",
#endif
  };

  for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    sdl.handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if(sdl.handle != nullptr) {
      break;
    }
  }

  if(sdl.handle == nullptr) {
    return false;
  }

  if(!load_symbol((void **)&sdl.Init, "SDL_Init") ||
     !load_symbol((void **)&sdl.Quit, "SDL_Quit") ||
     !load_symbol((void **)&sdl.GetError, "SDL_GetError") ||
     !load_symbol((void **)&sdl.CreateWindow, "SDL_CreateWindow") ||
     !load_symbol((void **)&sdl.CreateRenderer, "SDL_CreateRenderer") ||
     !load_symbol((void **)&sdl.CreateTexture, "SDL_CreateTexture") ||
     !load_symbol((void **)&sdl.DestroyTexture, "SDL_DestroyTexture") ||
     !load_symbol((void **)&sdl.DestroyRenderer, "SDL_DestroyRenderer") ||
     !load_symbol((void **)&sdl.DestroyWindow, "SDL_DestroyWindow") ||
     !load_symbol((void **)&sdl.UpdateTexture, "SDL_UpdateTexture") ||
     !load_symbol((void **)&sdl.RenderClear, "SDL_RenderClear") ||
     !load_symbol((void **)&sdl.RenderCopy, "SDL_RenderCopy") ||
     !load_symbol((void **)&sdl.RenderPresent, "SDL_RenderPresent") ||
     !load_symbol((void **)&sdl.PollEvent, "SDL_PollEvent")) {
    unload_sdl();
    return false;
  }

  return true;
}

static bool sdl_window_begin()
{
  if(display_mode != LVGL_PANEL_DISPLAY_WINDOW) {
    return true;
  }

  if(!load_sdl()) {
    Serial.println("[panel] SDL2 runtime unavailable for window display mode; install SDL2 to enable it");
    return false;
  }

  if(sdl.Init(SDL_INIT_VIDEO) != 0) {
    Serial.printf("[panel] SDL_Init failed: %s\n", sdl.GetError ? sdl.GetError() : "unknown");
    unload_sdl();
    return false;
  }

  sdl_window.window = sdl.CreateWindow("OpenEVSE LVGL",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W,
                                       SCREEN_H,
                                       SDL_WINDOW_SHOWN);
  if(sdl_window.window == nullptr) {
    Serial.printf("[panel] SDL_CreateWindow failed: %s\n", sdl.GetError ? sdl.GetError() : "unknown");
    unload_sdl();
    return false;
  }

  static const uint32_t renderer_flags[] = {
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
    SDL_RENDERER_ACCELERATED,
    SDL_RENDERER_SOFTWARE,
    0
  };

  for(size_t i = 0; i < sizeof(renderer_flags) / sizeof(renderer_flags[0]); i++) {
    sdl_window.renderer = sdl.CreateRenderer(sdl_window.window, -1, renderer_flags[i]);
    if(sdl_window.renderer != nullptr) {
      break;
    }
  }

  if(sdl_window.renderer == nullptr) {
    Serial.printf("[panel] SDL_CreateRenderer failed: %s\n", sdl.GetError ? sdl.GetError() : "unknown");
    unload_sdl();
    return false;
  }

  sdl_window.texture = sdl.CreateTexture(sdl_window.renderer,
                                         SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W,
                                         SCREEN_H);
  if(sdl_window.texture == nullptr) {
    Serial.printf("[panel] SDL_CreateTexture failed: %s\n", sdl.GetError ? sdl.GetError() : "unknown");
    unload_sdl();
    return false;
  }

  sdl_window.dirty = true;
  return true;
}
#endif

static bool lvgl_panel_prepare_begin(size_t buf_bytes)
{
  lv_init();

  buf1 = (lv_color_t *)malloc(buf_bytes);
  if(buf1 == nullptr) {
    Serial.printf("[panel] FATAL: draw-buffer alloc failed (%u B host heap)\n",
                  (unsigned)buf_bytes);
    return false;
  }

  host_fb = (lv_color_t *)calloc(SCREEN_W * SCREEN_H, sizeof(lv_color_t));
  if(host_fb == nullptr) {
    Serial.printf("[panel] FATAL: framebuffer alloc failed (%u B host heap)\n",
                  (unsigned)(SCREEN_W * SCREEN_H * sizeof(lv_color_t)));
    lvgl_panel_cleanup_host_allocations();
    return false;
  }

#if LVGL_PANEL_HAS_SDL
  if(!sdl_window_begin()) {
    lvgl_panel_cleanup_host_allocations();
    return false;
  }
#elif defined(EPOXY_DUINO)
  if(display_mode == LVGL_PANEL_DISPLAY_WINDOW) {
    Serial.println("[panel] window display mode unavailable: rebuild on a host with SDL2 headers installed");
    lvgl_panel_cleanup_host_allocations();
    return false;
  }
#endif

  return true;
}

void lvgl_panel_set_display_mode(LvglPanelDisplayMode mode)
{
  display_mode = mode;
}

LvglPanelDisplayMode lvgl_panel_get_display_mode()
{
  return display_mode;
}

const char *lvgl_panel_get_display_mode_name(LvglPanelDisplayMode mode)
{
  switch(mode) {
    case LVGL_PANEL_DISPLAY_WINDOW:
      return "window";
    case LVGL_PANEL_DISPLAY_HEADLESS:
    default:
      return "headless";
  }
}

void lvgl_panel_pump()
{
#if LVGL_PANEL_HAS_SDL
  if(display_mode != LVGL_PANEL_DISPLAY_WINDOW || sdl.handle == nullptr) {
    return;
  }

  SDL_Event event;
  while(sdl.PollEvent(&event)) {
    if(event.type == SDL_QUIT) {
      display_mode = LVGL_PANEL_DISPLAY_HEADLESS;
      unload_sdl();
      return;
    }
  }

  if(!sdl_window.dirty || host_fb == nullptr) {
    return;
  }

  if(sdl.UpdateTexture(sdl_window.texture, nullptr, host_fb, SCREEN_W * sizeof(lv_color_t)) != 0) {
    Serial.printf("[panel] SDL_UpdateTexture failed: %s\n", sdl.GetError ? sdl.GetError() : "unknown");
    display_mode = LVGL_PANEL_DISPLAY_HEADLESS;
    unload_sdl();
    return;
  }

  sdl.RenderClear(sdl_window.renderer);
  sdl.RenderCopy(sdl_window.renderer, sdl_window.texture, nullptr, nullptr);
  sdl.RenderPresent(sdl_window.renderer);
  sdl_window.dirty = false;
#else
  (void)0;
#endif
}

bool lvgl_panel_write_ppm(const char *path)
{
  if(host_fb == nullptr || path == nullptr || path[0] == '\0') {
    return false;
  }

  FILE *fp = fopen(path, "wb");
  if(fp == nullptr) {
    return false;
  }

  fprintf(fp, "P6\n%u %u\n255\n", SCREEN_W, SCREEN_H);
  for(uint32_t i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t px = host_fb[i].full;
    // RGB565 layout: RRRRRGGGGGGBBBBB. Expand each packed channel to 8-bit RGB.
    uint8_t rgb[3] = {
      (uint8_t)((((px >> 11) & 0x1F) * 255) / 31),
      (uint8_t)((((px >> 5)  & 0x3F) * 255) / 63),
      (uint8_t)(((px & 0x1F) * 255) / 31)
    };
    if(fwrite(rgb, sizeof(rgb), 1, fp) != 1) {
      fclose(fp);
      return false;
    }
  }
  fclose(fp);
  return true;
}

void lvgl_panel_set_backlight(uint8_t pct)
{
  (void)pct;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  if(host_fb) {
    int32_t x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t x2 = area->x2 >= SCREEN_W ? SCREEN_W - 1 : area->x2;
    int32_t y2 = area->y2 >= SCREEN_H ? SCREEN_H - 1 : area->y2;
    int32_t area_w = area->x2 - area->x1 + 1;

    if(x1 <= x2 && y1 <= y2) {
      for(int32_t y = y1; y <= y2; y++) {
        const lv_color_t *src = color_p + (y - area->y1) * area_w + (x1 - area->x1);
        lv_color_t *dst = host_fb + y * SCREEN_W + x1;
        memcpy(dst, src, (x2 - x1 + 1) * sizeof(lv_color_t));
      }
    }
  }
#if LVGL_PANEL_HAS_SDL
  sdl_window.dirty = true;
#endif
  lv_disp_flush_ready(drv);
}

#else

static TFT_eSPI tft = TFT_eSPI();

static bool lvgl_panel_prepare_begin(size_t buf_bytes)
{
  tft.init();
  tft.setRotation(1); // landscape, matches the original renderer
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(TFT_BL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
#else
  ledcSetup(LCD_BL_LEDC_CHANNEL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
  ledcAttachPin(TFT_BL, LCD_BL_LEDC_CHANNEL);
#endif
  // The backlight is independent of the LVGL draw buffer below, so it stays
  // usable even if that alloc fails and we return false — bl_ready is not cleared.
  bl_ready = true;
  lvgl_panel_set_backlight(100); // full on until LcdTask applies the configured level

#ifdef LCD_BL_PWM_SELFTEST
  // Gated diagnostic: ramp the backlight up/down a few times so a human can
  // confirm the panel actually dims (some BL circuits are on/off-only).
  // delay() is safe here: LVGL isn't running yet, and this is gated out of prod.
  for(int cycle = 0; cycle < 3; ++cycle) {
    for(int p = 0; p <= 100; p += 5) { lvgl_panel_set_backlight((uint8_t)p); delay(30); }
    for(int p = 100; p >= 0; p -= 5) { lvgl_panel_set_backlight((uint8_t)p); delay(30); }
  }
  lvgl_panel_set_backlight(100);
#endif

  lv_init();

  // MALLOC_CAP_INTERNAL is required, not a hint. On PSRAM boards a plain malloc()
  // of ~30 KB is over the SDK's 4096-byte ALWAYSINTERNAL threshold and would be
  // served from PSRAM -- which is exactly where the CPU-bound 3-byte-per-pixel
  // flush must not read from. The failure branch is a real boot-time mode, not a
  // formality: the SDK reserves no internal pool (SPIRAM_MALLOC_RESERVE_INTERNAL
  // is 0), so this competes with everything else on a fragmented heap. Keep the
  // largest-free-block report with it.
  buf1 = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if(buf1 == nullptr) {
    Serial.printf("[panel] FATAL: draw-buffer alloc failed (%u B internal); largest free block=%u\n",
                  (unsigned)buf_bytes,
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return false;
  }

  return true;
}

void lvgl_panel_set_backlight(uint8_t pct)
{
  if(!bl_ready) {
    return;
  }
  uint8_t duty = bl_pct_to_duty(pct);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(TFT_BL, duty);
#else
  ledcWrite(LCD_BL_LEDC_CHANNEL, duty);
#endif
}

// Convert a run of RGB565 pixels to the ILI9488's 18bpp wire format and push
// them in batches, instead of TFT_eSPI::pushPixels().
//
// The library's 18-bit pushPixels() is `while(len--) tft_Write_16(*data++)` --
// one whole SPI transaction per pixel, each programming mosi_dlen, writing the
// data register, setting cmd.usr and busy-waiting, all to move 24 bits. Measured
// on this board that is 4.48 us/px against 0.6 us of actual wire time at 40 MHz:
// 669 KB/s out of a 5 MB/s bus, and ~688 ms for a full screen. The library's
// non-18-bit path already batches into 512-bit FIFO writes; the 18-bit path
// never got the same treatment.
//
// SPIClass::writeBytes() does the chunked FIFO transfers for us, so the cost per
// pixel becomes the conversion plus a share of one transaction per chunk. A
// bigger buffer does not help once the per-transaction overhead is amortised
// over a couple of hundred pixels.
//
// The staging buffer is static, not on the stack. flush_cb() only ever runs on
// loopTask -- LVGL is driven from there -- so a single shared buffer is safe,
// and 768 B of BSS is the honest cost. On the stock TFT board the loop stack is
// 8 KB and already shared with Mongoose request handlers; quietly taking 768 B
// of it on a board that is in the field is not worth the cache locality.
static uint8_t push_buf[256 * 3];

static void push_pixels_batched(const uint16_t *src, uint32_t len)
{
  static const uint32_t CHUNK_PX = 256;          // 768 B staged per transfer
  uint8_t *buf = push_buf;

  SPIClass &spi = TFT_eSPI::getSPIinstance();

  while(len)
  {
    uint32_t n = (len < CHUNK_PX) ? len : CHUNK_PX;
    uint8_t *o = buf;
    for(uint32_t i = 0; i < n; i++)
    {
      // The draw buffer is byte-swapped: lv_conf.h sets LV_COLOR_16_SWAP=1 on
      // device, and TFT_eSPI's _swapBytes defaults to false, so pushPixels() was
      // taking its tft_Write_16S branch -- swap first, then extract. Reading the
      // halfword natively here scrambled every channel (inverted and grainy).
      uint16_t c = (uint16_t)((src[i] >> 8) | (src[i] << 8));
      // RGB565 -> RGB666, left-aligned in each byte exactly as tft_Write_16 does.
      *o++ = (uint8_t)((c & 0xF800) >> 8);
      *o++ = (uint8_t)((c & 0x07E0) >> 3);
      *o++ = (uint8_t)((c & 0x001F) << 3);
    }
    spi.writeBytes(buf, n * 3);
    src += n;
    len -= n;
  }
}

#ifdef LVGL_FLUSH_PROFILE
#include "debug.h"   // route the report through StreamSpy so /debug/console sees it
// Bench instrumentation for the display link, off unless -D LVGL_FLUSH_PROFILE.
//
// It splits the time inside pushPixels() -- the bytes actually clocked out, plus
// the CPU-side RGB565->RGB666 conversion this panel forces -- from the
// setAddrWindow()/startWrite() overhead around it. That is the measurement that
// says which lever is worth pulling: if push_us is close to the theoretical bus
// time for the bytes at SPI_FREQUENCY, the link is saturated and a clock bump is
// the answer; if it is well above, the per-pixel conversion dominates and the
// fix is esp_lcd, which converts and DMAs instead.
static uint32_t prof_flushes = 0;
static uint32_t prof_px      = 0;
static uint32_t prof_push_us = 0;
static uint32_t prof_win_us  = 0;
static uint32_t prof_last_report = 0;

static void flush_profile_report()
{
  uint32_t now = millis();
  if(prof_last_report != 0 && (now - prof_last_report) < 2000) {
    return;
  }
  prof_last_report = now;
  if(0 == prof_flushes || 0 == prof_px) {
    return;
  }

  // 3 bytes/pixel on the wire: this panel is 18bpp over SPI, RGB565 is not an
  // option. A "frame" here is one full screen's worth of pixels, whether or not
  // any single flush covered that much -- LVGL only ever flushes dirty areas.
  uint32_t bytes   = prof_px * 3;
  uint32_t total_us = prof_push_us + prof_win_us;
  uint32_t kbps    = prof_push_us ? (uint32_t)(((uint64_t)bytes * 1000ULL) / prof_push_us) : 0;
  uint32_t frame_ms = (uint32_t)(((uint64_t)total_us * SCREEN_W * SCREEN_H) / ((uint64_t)prof_px * 1000ULL));

  DBUGF("[lvgl] %lu flushes, %lu px, push %lu us, win %lu us, %lu KB/s, full frame ~%lu ms (~%lu fps) @ %d Hz",
        (unsigned long)prof_flushes, (unsigned long)prof_px,
        (unsigned long)prof_push_us, (unsigned long)prof_win_us,
        (unsigned long)kbps, (unsigned long)frame_ms,
        (unsigned long)(frame_ms ? 1000 / frame_ms : 0), (int)SPI_FREQUENCY);

  prof_flushes = 0; prof_px = 0; prof_push_us = 0; prof_win_us = 0;
}
#endif

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#ifdef LVGL_FLUSH_PROFILE
  uint32_t t0 = micros();
#endif
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
#ifdef LVGL_FLUSH_PROFILE
  uint32_t t1 = micros();
#endif
  push_pixels_batched((uint16_t *)&color_p->full, w * h);
#ifdef LVGL_FLUSH_PROFILE
  uint32_t t2 = micros();
#endif
  tft.endWrite();

#ifdef LVGL_FLUSH_PROFILE
  prof_flushes++;
  prof_px      += w * h;
  prof_win_us  += (t1 - t0);
  prof_push_us += (t2 - t1);
  flush_profile_report();
#endif

  lv_disp_flush_ready(drv);
}

#endif

bool lvgl_panel_begin()
{
  const size_t buf_bytes = DRAW_BUF_PIXELS * sizeof(lv_color_t);

  if(!lvgl_panel_prepare_begin(buf_bytes)) {
    return false;
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, DRAW_BUF_PIXELS);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

#if defined(EPOXY_DUINO)
  Serial.printf("[panel] %s LVGL display up %ux%u, 1 buf %u B host heap\n",
                lvgl_panel_get_display_mode_name(display_mode),
                SCREEN_W, SCREEN_H, (unsigned)buf_bytes);
#else
  Serial.printf("[panel] display up %ux%u, 1 buf %u B internal, free internal heap=%u\n",
                SCREEN_W, SCREEN_H, (unsigned)buf_bytes,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
  return true;
}

#endif // ENABLE_SCREEN_LVGL_TFT
