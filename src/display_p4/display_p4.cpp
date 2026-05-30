#if defined(ENABLE_SCREEN_LVGL)

#include <Arduino.h>
#include "lvgl.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"

#include "display_p4.h"
#include "panel_st7701_dsi.h"
#include "touch/gt911_touch.h"
#include "evse_ui_model.h"
#include "evse_ui_command.h"
#include "evse_man.h"
#include "manual.h"

#ifndef DISPLAY_P4_BACKLIGHT_PIN
#define DISPLAY_P4_BACKLIGHT_PIN 23
#endif
#ifndef DISPLAY_P4_BACKLIGHT_TIMEOUT_MS
#define DISPLAY_P4_BACKLIGHT_TIMEOUT_MS 120000UL
#endif

#define DP4_TP_SDA   7
#define DP4_TP_SCL   8
#define DP4_BL_FREQ  5000
#define DP4_BL_RES   8
#define DP4_BL_FULL  255

static St7701DsiPanel s_panel;
static gt911_touch s_touch = gt911_touch(DP4_TP_SDA, DP4_TP_SCL, -1, -1);

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1 = NULL;
static lv_color_t *s_buf2 = NULL;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static volatile bool s_activity = false;   // set by touch read_cb, consumed by loop()

// --- bring-up screen (carried from D1) ---
static lv_obj_t *s_touch_count_label = NULL;
static uint32_t s_touch_count = 0;

static IEvseUiModel *s_model = NULL;
static IEvseUiCommandSink *s_cmd = NULL;
static lv_obj_t *s_state_label = NULL;   // live state text
static lv_obj_t *s_values_label = NULL;  // live V / A / kW

static void dp4_btn_event_cb(lv_event_t *e)
{
  if (s_cmd) {
    s_cmd->toggleCharge();
  }
  s_touch_count++;
  if (s_touch_count_label) {
    lv_label_set_text_fmt(s_touch_count_label, "Toggles: %u", (unsigned)s_touch_count);
  }
}

static void dp4_build_bringup_screen(void)
{
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "OpenEVSE  ESP32-P4");
  lv_obj_set_style_text_color(title, lv_color_hex(0x33C481), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

  s_state_label = lv_label_create(scr);
  lv_label_set_text(s_state_label, "State: ...");
  lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, 64);

  s_values_label = lv_label_create(scr);
  lv_label_set_text(s_values_label, "0.0 V   0.00 A   0.000 kW");
  lv_obj_set_style_text_color(s_values_label, lv_color_hex(0x8A9099), LV_PART_MAIN);
  lv_obj_align(s_values_label, LV_ALIGN_TOP_MID, 0, 90);

  lv_obj_t *arc = lv_arc_create(scr);
  lv_obj_set_size(arc, 220, 220);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, 66);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, -40);

  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 200, 64);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 150);
  lv_obj_add_event_cb(btn, dp4_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *btn_lbl = lv_label_create(btn);
  lv_label_set_text(btn_lbl, "TAP ME");
  lv_obj_center(btn_lbl);

  s_touch_count = 0;
  s_touch_count_label = lv_label_create(scr);
  lv_label_set_text(s_touch_count_label, "Toggles: 0");
  lv_obj_set_style_text_color(s_touch_count_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_align(s_touch_count_label, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// --- LVGL callbacks ---
static void dp4_flush_ready_cb(void *arg)
{
  lv_disp_drv_t *drv = (lv_disp_drv_t *)arg;
  lv_disp_flush_ready(drv);
}

static void dp4_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  s_panel.flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, &color_p->full);
}

static void dp4_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  uint16_t x = 0, y = 0;
  if (s_touch.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    s_activity = true;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

DisplayP4Task displayP4;

DisplayP4Task::DisplayP4Task()
  : MicroTasks::Task(), _backlightDeadline(0), _backlightOn(false), _lastModelRefresh(0)
{
}

void DisplayP4Task::begin(EvseManager &evse, ManualOverride &manual)
{
  static EvseUiModel model(evse);
  static EvseUiCommandSink cmd(manual);
  s_model = &model;
  s_cmd = &cmd;
  MicroTask.startTask(this);
}

void DisplayP4Task::wakeBacklight()
{
  s_activity = true;
}

void DisplayP4Task::setup()
{
  // 1) Shared I2C bus on port 1 (GPIO7/8) BEFORE touch.begin() (GT911 driver
  //    fetches it via i2c_master_get_bus_handle(1, ...)).
  i2c_master_bus_handle_t i2c_handle = NULL;
  i2c_master_bus_config_t i2c_cfg = {};
  i2c_cfg.i2c_port = I2C_NUM_1;
  i2c_cfg.sda_io_num = (gpio_num_t)DP4_TP_SDA;
  i2c_cfg.scl_io_num = (gpio_num_t)DP4_TP_SCL;
  i2c_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  i2c_cfg.glitch_ignore_cnt = 7;
  i2c_cfg.flags.enable_internal_pullup = 1;
  i2c_new_master_bus(&i2c_cfg, &i2c_handle);

  // 2) Panel (via HAL) + touch.
  s_panel.begin();
  s_touch.begin();

  // 3) LVGL core + full-screen double buffers in PSRAM.
  lv_init();
  size_t buf_px = (size_t)s_panel.width() * s_panel.height();
  s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  s_buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  assert(s_buf1 && s_buf2);
  lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_px);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = s_panel.width();
  s_disp_drv.ver_res = s_panel.height();
  s_disp_drv.flush_cb = dp4_flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  s_disp_drv.full_refresh = false;
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = dp4_touch_read_cb;
  lv_indev_drv_register(&s_indev_drv);

  s_panel.onFlushReady(dp4_flush_ready_cb, &s_disp_drv);

  // 4) Content.
  dp4_build_bringup_screen();

  // 5) Backlight on LEDC PWM, full brightness, start the idle timer.
  ledcAttach(DISPLAY_P4_BACKLIGHT_PIN, DP4_BL_FREQ, DP4_BL_RES);
  ledcWrite(DISPLAY_P4_BACKLIGHT_PIN, DP4_BL_FULL);
  _backlightOn = true;
  _backlightDeadline = millis() + DISPLAY_P4_BACKLIGHT_TIMEOUT_MS;
}

static void dp4_refresh_model_view(void)
{
  if (!s_model) return;
  if (s_state_label) {
    lv_label_set_text_fmt(s_state_label, "State: %s%s", s_model->stateText(),
                          s_model->vehicleConnected() ? "  (EV)" : "");
  }
  if (s_values_label) {
    lv_label_set_text_fmt(s_values_label, "%.1f V   %.2f A   %.3f kW",
                          s_model->voltage(), s_model->amps(), s_model->power() / 1000.0);
  }
}

unsigned long DisplayP4Task::loop(MicroTasks::WakeReason reason)
{
  lv_timer_handler();

  unsigned long now = millis();
  if (now - _lastModelRefresh >= 500) {
    _lastModelRefresh = now;
    dp4_refresh_model_view();
  }

  if (s_activity) {
    s_activity = false;
    if (!_backlightOn) {
      ledcWrite(DISPLAY_P4_BACKLIGHT_PIN, DP4_BL_FULL);
      _backlightOn = true;
    }
    _backlightDeadline = now + DISPLAY_P4_BACKLIGHT_TIMEOUT_MS;
  } else if (_backlightOn && (long)(now - _backlightDeadline) >= 0) {
    ledcWrite(DISPLAY_P4_BACKLIGHT_PIN, 0);
    _backlightOn = false;
  }

  return 5;  // pump LVGL ~every 5 ms, cooperatively
}

#endif // ENABLE_SCREEN_LVGL
