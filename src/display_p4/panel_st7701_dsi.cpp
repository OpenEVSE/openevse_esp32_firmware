#if defined(ENABLE_SCREEN_LVGL)

#include "panel_st7701_dsi.h"

St7701DsiPanel::St7701DsiPanel()
  : _lcd(-1), _readyCb(nullptr), _readyArg(nullptr)
{
}

bool St7701DsiPanel::dsiDoneTrampoline(esp_lcd_panel_handle_t panel,
                                       esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
  St7701DsiPanel *self = (St7701DsiPanel *)user_ctx;
  if (self->_readyCb) {
    self->_readyCb(self->_readyArg);
  }
  return false;
}

void St7701DsiPanel::begin()
{
  _lcd.begin();
  _lcd.get_handle(&_handles);

  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = dsiDoneTrampoline;
  esp_lcd_dpi_panel_register_event_callbacks(_handles.panel, &cbs, this);
}

void St7701DsiPanel::flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t *pixels)
{
  _lcd.lcd_draw_bitmap(x1, y1, x2, y2, pixels);
}

void St7701DsiPanel::onFlushReady(panel_flush_ready_cb_t cb, void *arg)
{
  _readyCb = cb;
  _readyArg = arg;
}

#endif // ENABLE_SCREEN_LVGL
