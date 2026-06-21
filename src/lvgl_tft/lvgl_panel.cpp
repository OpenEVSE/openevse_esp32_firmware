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
#endif

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
#if defined(EPOXY_DUINO)
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

#if defined(EPOXY_DUINO)
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
    "libSDL2-2.0.so.0",
    "libSDL2.so"
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
    Serial.println("[panel] SDL2 runtime unavailable for window display mode");
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
#endif
}
#endif

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

  host_fb = (lv_color_t *)calloc(SCREEN_W * SCREEN_H, sizeof(lv_color_t));
  if(host_fb == nullptr) {
    Serial.printf("[panel] FATAL: framebuffer alloc failed (%u B host heap)\n",
                  (unsigned)(SCREEN_W * SCREEN_H * sizeof(lv_color_t)));
    free(buf1);
    buf1 = nullptr;
    return false;
  }

#if LVGL_PANEL_HAS_SDL
  if(!sdl_window_begin()) {
    free(host_fb);
    host_fb = nullptr;
    free(buf1);
    buf1 = nullptr;
    return false;
  }
#elif defined(EPOXY_DUINO)
  if(display_mode == LVGL_PANEL_DISPLAY_WINDOW) {
    Serial.println("[panel] window display mode requires SDL2 headers at build time");
    free(host_fb);
    host_fb = nullptr;
    free(buf1);
    buf1 = nullptr;
    return false;
  }
#endif
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

#if defined(EPOXY_DUINO)
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
#endif

#endif // ENABLE_SCREEN_LVGL_TFT
