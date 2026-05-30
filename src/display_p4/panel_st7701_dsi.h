#ifndef DISPLAY_P4_PANEL_ST7701_DSI_H
#define DISPLAY_P4_PANEL_ST7701_DSI_H
#if defined(ENABLE_SCREEN_LVGL)

#include "esp_lcd_mipi_dsi.h"
#include "panel.h"
#include "lcd/st7701_lcd.h"

class St7701DsiPanel : public IDisplayPanel
{
public:
  St7701DsiPanel();
  void begin() override;
  int16_t width() override { return 480; }
  int16_t height() override { return 800; }
  void flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t *pixels) override;
  void onFlushReady(panel_flush_ready_cb_t cb, void *arg) override;

private:
  st7701_lcd _lcd;
  bsp_lcd_handles_t _handles;
  panel_flush_ready_cb_t _readyCb;
  void *_readyArg;
  static bool dsiDoneTrampoline(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx);
};

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_PANEL_ST7701_DSI_H
