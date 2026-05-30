#ifndef DISPLAY_P4_PANEL_H
#define DISPLAY_P4_PANEL_H
#if defined(ENABLE_SCREEN_LVGL)

#include <stdint.h>

typedef void (*panel_flush_ready_cb_t)(void *arg);

// Panel/flush HAL (design spec seam #3): decouples the LVGL UI from the
// concrete display backend (ST7701 MIPI-DSI here; legacy SPI elsewhere).
class IDisplayPanel
{
public:
  virtual ~IDisplayPanel() {}
  virtual void begin() = 0;
  virtual int16_t width() = 0;
  virtual int16_t height() = 0;
  // Push a rectangle of 16-bit (RGB565) pixels. x2/y2 are exclusive-end
  // (pass area->x2 + 1 / area->y2 + 1). The backend may transfer asynchronously
  // and MUST invoke the flush-ready callback (registered via onFlushReady) when
  // the transfer completes.
  virtual void flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t *pixels) = 0;
  virtual void onFlushReady(panel_flush_ready_cb_t cb, void *arg) = 0;
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_PANEL_H
