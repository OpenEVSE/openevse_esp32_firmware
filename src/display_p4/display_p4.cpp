#if defined(ENABLE_SCREEN_LVGL)

#include <Arduino.h>
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"

#include "lcd/st7701_lcd.h"
#include "touch/gt911_touch.h"
#include "display_p4.h"

#define DP4_H_RES 480
#define DP4_V_RES 800
#define DP4_TP_SDA 7
#define DP4_TP_SCL 8

static st7701_lcd s_lcd = st7701_lcd(-1);                 // panel reset is GPIO5 inside the driver
static gt911_touch s_touch = gt911_touch(DP4_TP_SDA, DP4_TP_SCL, -1, -1);
static bsp_lcd_handles_t s_panels;

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

// DSI "color transfer done" -> LVGL flush complete.
static bool dp4_dpi_flush_done(esp_lcd_panel_handle_t panel,
                               esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
  lv_disp_drv_t *drv = (lv_disp_drv_t *)user_ctx;
  lv_disp_flush_ready(drv);
  return false;
}

static void dp4_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  s_lcd.lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, &color_p->full);
}

static void dp4_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  uint16_t x = 0, y = 0;
  if (s_touch.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void dp4_lvgl_task(void *arg)
{
  for (;;) {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void display_p4_begin()
{
  // 1) Shared I2C bus on port 1 (GPIO7/8). GT911 driver fetches this via
  //    i2c_master_get_bus_handle(1, ...) so it MUST exist before touch.begin().
  i2c_master_bus_handle_t i2c_handle = NULL;
  i2c_master_bus_config_t i2c_cfg = {};
  i2c_cfg.i2c_port = I2C_NUM_1;
  i2c_cfg.sda_io_num = (gpio_num_t)DP4_TP_SDA;
  i2c_cfg.scl_io_num = (gpio_num_t)DP4_TP_SCL;
  i2c_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  i2c_cfg.glitch_ignore_cnt = 7;
  i2c_cfg.flags.enable_internal_pullup = 1;
  i2c_new_master_bus(&i2c_cfg, &i2c_handle);

  // 2) Panel + touch.
  s_lcd.begin();
  s_touch.begin();
  s_lcd.get_handle(&s_panels);

  // 3) LVGL core + full-screen double buffers in PSRAM.
  lv_init();
  size_t buf_px = (size_t)DP4_H_RES * DP4_V_RES;
  s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  s_buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  assert(s_buf1 && s_buf2);
  lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_px);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = DP4_H_RES;
  s_disp_drv.ver_res = DP4_V_RES;
  s_disp_drv.flush_cb = dp4_flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  s_disp_drv.full_refresh = false;
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = dp4_touch_read_cb;
  lv_indev_drv_register(&s_indev_drv);

  // 4) Bind the DSI done-callback to flush completion.
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = dp4_dpi_flush_done;
  esp_lcd_dpi_panel_register_event_callbacks(s_panels.panel, &cbs, &s_disp_drv);

  // 5) D1 bring-up content.
  lv_demo_widgets();

  // 6) Pump LVGL from its own task (D2 will migrate this to a MicroTasks::Task).
  xTaskCreatePinnedToCore(dp4_lvgl_task, "lvgl", 8192, NULL, 2, NULL, 1);
}

#endif // ENABLE_SCREEN_LVGL
