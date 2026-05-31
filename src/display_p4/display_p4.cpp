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
#include "p4_screen_manager.h"

#if defined(ENABLE_EEZ_UI)
#include "ui.h"   // EEZ Studio export (eez-ui-export/, see eez_ui_unit.c)
#endif

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

// Landscape orientation. The ST7701 is driven over MIPI-DSI/DPI, whose
// framebuffer scans out in a fixed 480x800 (portrait) raster — the panel
// driver exposes mirror() but no swap_xy(), so there is no hardware rotation.
// We therefore rotate in LVGL software: the physical resolution stays 480x800
// and LVGL presents an 800x480 landscape canvas, rotating each rendered area
// into panel coordinates before flush. This requires full_refresh = false
// (LVGL cannot software-rotate a full-refresh display). Touch needs no extra
// work — lv_indev transforms the raw panel coordinates using `rotated`.
// Flip to LV_DISP_ROT_270 (e.g. -D DISPLAY_P4_ROTATION=LV_DISP_ROT_270) if the
// image is upside-down for the chosen mounting.
#ifndef DISPLAY_P4_ROTATION
#define DISPLAY_P4_ROTATION LV_DISP_ROT_90
#endif

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

static P4ScreenManager *s_screens = NULL;
static lv_obj_t *s_screen_label = NULL;
static unsigned long s_bootMs = 0;

#if defined(ENABLE_EEZ_UI)
static void dp4_eez_btn_toggle_cb(lv_event_t *e);   // defined below; used in setup()
static void dp4_eez_sync_screen(void);              // defined below; used in setup()
static void dp4_attach_breathe(lv_obj_t *ring);     // defined below; used in setup()
static void dp4_make_status_cluster(void);          // defined below; used in setup()
static void dp4_update_status_cluster(IEvseUiModel *m, bool conn);  // used in dp4_eez_update()
static lv_obj_t *s_top_wifi = NULL;                 // wifi glyph on the top layer (all screens)
static lv_obj_t *s_top_evse = NULL;                 // EVSE status dot on the top layer
#endif

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

  s_screen_label = lv_label_create(scr);
  lv_label_set_text(s_screen_label, "Screen: boot");
  lv_obj_set_style_text_color(s_screen_label, lv_color_hex(0x38BDF8), LV_PART_MAIN);
  lv_obj_align(s_screen_label, LV_ALIGN_BOTTOM_MID, 0, -60);
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
  static P4ScreenManager screens(model);
  s_screens = &screens;
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
  s_disp_drv.hor_res = s_panel.width();    // physical (portrait) resolution
  s_disp_drv.ver_res = s_panel.height();
  s_disp_drv.flush_cb = dp4_flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  s_disp_drv.full_refresh = false;         // required for software rotation
  s_disp_drv.sw_rotate = 1;                // DSI has no HW rotation; rotate in LVGL
  s_disp_drv.rotated = DISPLAY_P4_ROTATION; // -> 800x480 landscape logical canvas
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = dp4_touch_read_cb;
  lv_indev_drv_register(&s_indev_drv);

  s_panel.onFlushReady(dp4_flush_ready_cb, &s_disp_drv);

  // 4) Content.
#if defined(ENABLE_EEZ_UI)
  ui_init();                 // build EEZ screens; loads the default screen
  lv_obj_add_event_cb(objects.btn_start_stop, dp4_eez_btn_toggle_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(objects.btn_wake, dp4_eez_btn_toggle_cb, LV_EVENT_CLICKED, NULL);
  dp4_eez_sync_screen();     // set the initial screen (boot) from the manager

  // Breathe the EEZ-designed indicator rings on the passive states.
  dp4_attach_breathe(objects.sleeping_ring);
  dp4_attach_breathe(objects.fault_ring);

  // Wifi + EVSE-status indicators on the top layer (shown on every screen). The
  // per-screen EEZ wifi label is now redundant -> hide it.
  dp4_make_status_cluster();
  lv_obj_add_flag(objects.charge_wifi_label, LV_OBJ_FLAG_HIDDEN);
#else
  dp4_build_bringup_screen();
#endif

  // 5) Backlight on LEDC PWM, full brightness, start the idle timer.
  ledcAttach(DISPLAY_P4_BACKLIGHT_PIN, DP4_BL_FREQ, DP4_BL_RES);
  ledcWrite(DISPLAY_P4_BACKLIGHT_PIN, DP4_BL_FULL);
  _backlightOn = true;
  _backlightDeadline = millis() + DISPLAY_P4_BACKLIGHT_TIMEOUT_MS;

  s_bootMs = millis();
}

#if defined(ENABLE_EEZ_UI)
// START/STOP button on screen_charge -> toggle the charge override.
static void dp4_eez_btn_toggle_cb(lv_event_t *e)
{
  (void) e;
  if (s_cmd) s_cmd->toggleCharge();
}

// Push live IEvseUiModel values into the EEZ screen_charge widgets (~2 Hz).
// Bound here (vs. inside the EEZ export) so the generated UI stays pure layout.
// NOTE: only screen_charge widgets are bound for now -- boot/sleeping/fault and
// screen switching land once those screens are in the regenerated export.
static void dp4_eez_update(void)
{
  IEvseUiModel *m = s_model;
  if (!m) return;

  bool conn = m->evseConnected();

  // screen_charge values -- '--' when the EVSE controller is offline.
  lv_label_set_text(objects.charge_state_label, conn ? m->stateText() : "No EVSE");
  if (conn) {
    lv_label_set_text_fmt(objects.charge_kw_value, "%.2f", m->power() / 1000.0);
    lv_label_set_text_fmt(objects.charge_amps_value, "%.1f A", m->amps());
    lv_label_set_text_fmt(objects.charge_volts_value, "%.1f V", m->voltage());
    lv_label_set_text_fmt(objects.charge_energy_value, "%.2f kWh", m->sessionEnergy() / 1000.0);
    uint32_t secs = m->sessionElapsed();
    lv_label_set_text_fmt(objects.charge_elapsed_value, "%u:%02u:%02u",
                          (unsigned)(secs / 3600), (unsigned)((secs / 60) % 60), (unsigned)(secs % 60));
    lv_label_set_text_fmt(objects.charge_rate_value, "%u A", (unsigned)m->pilotCurrent());
    if (m->tempValid()) lv_label_set_text_fmt(objects.charge_temp_value, "%.1f C", m->temperatureC());
    else                lv_label_set_text(objects.charge_temp_value, "--");
  } else {
    lv_label_set_text(objects.charge_kw_value, "--");
    lv_label_set_text(objects.charge_amps_value, "--");
    lv_label_set_text(objects.charge_volts_value, "--");
    lv_label_set_text(objects.charge_energy_value, "--");
    lv_label_set_text(objects.charge_elapsed_value, "--");
    lv_label_set_text(objects.charge_rate_value, "--");
    lv_label_set_text(objects.charge_temp_value, "--");
  }

  // Power ring: fraction of the pilot (charge-current limit) drawn (0 if offline).
  int pct = 0;
  if (conn) {
    uint32_t pilot = m->pilotCurrent();
    if (pilot > 0) {
      pct = (int)((m->amps() / (double)pilot) * 100.0 + 0.5);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
    }
  }
  lv_arc_set_value(objects.charge_power_ring, pct);

  lv_label_set_text(objects.btn_start_stop_label, m->active() ? "STOP" : "START");

  // --- other screens (set unconditionally; they render when shown) ---
  lv_label_set_text(objects.boot_status_label, m->stateText());
  {
    uint32_t bms = (s_bootMs && millis() > s_bootMs) ? (millis() - s_bootMs) : 0;
    int bpct = (int)(bms * 100UL / 3000UL);   // ramp over the ~3s boot window
    if (bpct > 100) bpct = 100;
    lv_bar_set_value(objects.boot_progress, bpct, LV_ANIM_OFF);
  }
  lv_label_set_text(objects.sleeping_state_label, m->stateText());
  lv_label_set_text(objects.fault_text_label, m->stateText());

  // Wifi + EVSE-status indicators live on the top layer -> shown on every screen.
  dp4_update_status_cluster(m, conn);
}

// Map the P4ScreenManager's logical screen to an EEZ screen; load on change.
static int s_eez_loaded_screen = -1;
static void dp4_eez_sync_screen(void)
{
  if (!s_screens) return;
  int target;
  switch (s_screens->current()) {
    case P4Screen::Boot:     target = SCREEN_ID_SCREEN_BOOT;     break;
    case P4Screen::Sleeping: target = SCREEN_ID_SCREEN_SLEEPING; break;
    case P4Screen::Fault:    target = SCREEN_ID_SCREEN_FAULT;    break;
    case P4Screen::Charge:
    default:                 target = SCREEN_ID_SCREEN_CHARGE;   break;
  }
  if (target != s_eez_loaded_screen) {
    s_eez_loaded_screen = target;
    loadScreen((enum ScreensEnum)target);
  }
}

// Slow "breathing" pulse mimicking the web UI ring on the sleeping/fault states
// (opacity 1->~0.45 + scale 1->0.96, ease-in-out, ~2.2s ping-pong, forever).
// anim value: 0 = full, 1000 = dim+small.
static void dp4_breathe_anim_cb(void *var, int32_t v)
{
  // Opacity-only breathe on the arc's ring: 100% -> ~35%, ease-in-out ping-pong.
  lv_obj_set_style_arc_opa((lv_obj_t *)var, (lv_opa_t)(255 - (v * (255 - 90)) / 1000),
                           LV_PART_MAIN);
}

// Add a colour-coded ring centred on `parent` (behind its content) that breathes.
// Attach the opacity breathe to an EEZ-designed ring (sleeping_ring/fault_ring).
// The arc's colour/size/thickness/position come from the EEZ project now; we
// only animate its arc opacity.
static void dp4_attach_breathe(lv_obj_t *ring)
{
  if (!ring) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ring);
  lv_anim_set_exec_cb(&a, dp4_breathe_anim_cb);
  lv_anim_set_values(&a, 0, 1000);
  lv_anim_set_time(&a, 1100);
  lv_anim_set_playback_time(&a, 1100);   // -> 2.2s full cycle
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// Status cluster (wifi glyph + EVSE-status dot) on LVGL's top layer, so it shows
// on EVERY screen at a fixed top-right spot. Created once in setup().
static void dp4_make_status_cluster(void)
{
  lv_obj_t *top = lv_layer_top();

  s_top_evse = lv_obj_create(top);                 // EVSE status dot (left of wifi)
  lv_obj_remove_style_all(s_top_evse);
  lv_obj_set_size(s_top_evse, 14, 14);
  lv_obj_set_pos(s_top_evse, 730, 17);
  lv_obj_clear_flag(s_top_evse, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(s_top_evse, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_top_evse, LV_OPA_COVER, LV_PART_MAIN);

  s_top_wifi = lv_label_create(top);               // wifi glyph
  lv_obj_set_pos(s_top_wifi, 756, 14);
  lv_label_set_text(s_top_wifi, LV_SYMBOL_WIFI);
}

// Recolour the top-layer indicators from the active theme each refresh.
static void dp4_update_status_cluster(IEvseUiModel *m, bool conn)
{
  if (s_top_wifi) {
    int cid = m->wifiConnected() ? COLOR_ID_SUCCESS   // green
            : m->wifiApMode()    ? COLOR_ID_SLEEP      // blue (AP)
                                 : COLOR_ID_ERROR;     // red
    lv_obj_set_style_text_color(s_top_wifi, lv_color_hex(theme_colors[active_theme_index][cid]),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (s_top_evse) {
    // Mirror the web status dot: charging=accent, fault/offline=red, else dim.
    int cid = !conn         ? COLOR_ID_ERROR
            : m->error()    ? COLOR_ID_ERROR
            : m->charging() ? COLOR_ID_CHARGING
                            : COLOR_ID_TEXT_DIM;
    lv_obj_set_style_bg_color(s_top_evse, lv_color_hex(theme_colors[active_theme_index][cid]),
                              LV_PART_MAIN);
  }
}
#endif

static void dp4_refresh_model_view(void)
{
  if (!s_model) return;
#if defined(ENABLE_EEZ_UI)
  dp4_eez_update();
#endif
  if (s_state_label) {
    lv_label_set_text_fmt(s_state_label, "State: %s%s", s_model->stateText(),
                          s_model->vehicleConnected() ? "  (EV)" : "");
  }
  if (s_values_label) {
    lv_label_set_text_fmt(s_values_label, "%.1f V   %.2f A   %.3f kW",
                          s_model->voltage(), s_model->amps(), s_model->power() / 1000.0);
  }
  if (s_screens) {
    if (s_bootMs != 0 && (millis() - s_bootMs) > 3000) {
      s_screens->markBooted();
    }
    s_screens->update();
    if (s_screen_label) {
      lv_label_set_text_fmt(s_screen_label, "Screen: %s",
                            P4ScreenManager::name(s_screens->current()));
    }
#if defined(ENABLE_EEZ_UI)
    dp4_eez_sync_screen();   // switch EEZ screen to match the EVSE state
#endif
  }
}

unsigned long DisplayP4Task::loop(MicroTasks::WakeReason reason)
{
#if defined(ENABLE_EEZ_UI)
  ui_tick();                 // drive EEZ screen updates before LVGL renders
#endif
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
