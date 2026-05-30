# P4 Display D2: Panel/flush HAL + cooperative pump + LEDC backlight — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Harden the D1 display bring-up: (1) introduce a thin **panel/flush HAL** (spec §6 seam #3) so the LVGL UI is decoupled from the DSI backend; (2) pump LVGL from a **`MicroTasks::Task`** (cooperative with net/web) instead of a raw FreeRTOS task; (3) drive the backlight via **LEDC PWM (GPIO23)** with an **idle timeout** that wakes on touch.

**Architecture:** Extract the ST7701 DSI panel behind an `IDisplayPanel` interface. A `DisplayP4Task : public MicroTasks::Task` owns LVGL (buffers, display + input drivers, the bring-up screen) and the backlight, calling the panel through the interface. The DSI "color-transfer-done" callback flows back through the panel's `onFlushReady` hook to `lv_disp_flush_ready`. The legacy SPI effort can later supply its own `IDisplayPanel` implementation and bind the same UI.

**Tech Stack:** unchanged from D1 (LVGL 8.x, esp_lcd MIPI-DSI, MicroTasks, core-3 LEDC).

**Builds on D1** (commits c59a0b0→6d6f76f). All work stays gated behind `ENABLE_SCREEN_LVGL` (only in `[env:openevse_p4]`).

**Conventions to match (verified in-repo):**
- `MicroTasks::Task`: protected `void setup()` + `unsigned long loop(MicroTasks::WakeReason)` (returns ms to next call); public `begin()` calls `MicroTask.startTask(this)`; started from `main.cpp setup()` like `lcd.begin(...)`.
- core-3 LEDC: `ledcAttach(pin, freq, res)` then `ledcWrite(pin, duty)` (P4 is always core-3; no version guard needed here).
- Backlight idle pattern (from `screen_manager.cpp`): a `millis()` deadline, refreshed on activity, expires → backlight off.

---

## File Structure

- Create `src/display_p4/panel.h` — `IDisplayPanel` interface (the HAL seam).
- Create `src/display_p4/panel_st7701_dsi.h` / `.cpp` — `St7701DsiPanel` (wraps vendor `st7701_lcd` + DSI done callback).
- Rewrite `src/display_p4/display_p4.h` / `.cpp` — `DisplayP4Task` (MicroTasks::Task) owning LVGL + backlight, using `IDisplayPanel`. Replaces the D1 free function + raw task.
- Modify `src/main.cpp` — `display_p4_begin();` → `displayP4.begin();` (guarded).

---

## Task 1: Panel/flush HAL interface + ST7701 DSI implementation

**Files:** Create `src/display_p4/panel.h`, `src/display_p4/panel_st7701_dsi.h`, `src/display_p4/panel_st7701_dsi.cpp`

- [ ] **Step 1: Create `src/display_p4/panel.h`**
```cpp
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
```

- [ ] **Step 2: Create `src/display_p4/panel_st7701_dsi.h`**
```cpp
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
```

- [ ] **Step 3: Create `src/display_p4/panel_st7701_dsi.cpp`**
```cpp
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
```

- [ ] **Step 4: Build** — `pio run -e openevse_p4 2>&1 | tail -20`. Expect SUCCESS (these new files compile but aren't referenced yet; display_p4.cpp from D1 is still the active path). If the DSI callback API differs on IDF 5.5.4, match `esp_lcd_mipi_dsi.h` (it matched exactly in D1, so no change expected).

- [ ] **Step 5: Commit** — `git add src/display_p4/panel.h src/display_p4/panel_st7701_dsi.h src/display_p4/panel_st7701_dsi.cpp && git commit -m "p4 display: add panel/flush HAL (IDisplayPanel) + ST7701 DSI backend"`

---

## Task 2: DisplayP4Task (MicroTasks pump) + LEDC backlight, via the HAL

**Files:** Rewrite `src/display_p4/display_p4.h` and `src/display_p4/display_p4.cpp`; modify `src/main.cpp`.

- [ ] **Step 1: Rewrite `src/display_p4/display_p4.h`**
```cpp
#ifndef DISPLAY_P4_H
#define DISPLAY_P4_H
#if defined(ENABLE_SCREEN_LVGL)

#include <MicroTasks.h>

// LVGL display + GT911 touch + backlight, pumped cooperatively by the MicroTask
// scheduler. Replaces the D1 free function + raw FreeRTOS task.
class DisplayP4Task : public MicroTasks::Task
{
public:
  DisplayP4Task();
  void begin();           // register with the MicroTask scheduler
  void wakeBacklight();   // call on user activity to re-light + reset the idle timer

protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);

private:
  unsigned long _backlightDeadline;
  bool _backlightOn;
};

extern DisplayP4Task displayP4;

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_H
```

- [ ] **Step 2: Rewrite `src/display_p4/display_p4.cpp`**
```cpp
#if defined(ENABLE_SCREEN_LVGL)

#include <Arduino.h>
#include "lvgl.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"

#include "display_p4.h"
#include "panel_st7701_dsi.h"
#include "touch/gt911_touch.h"

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

static void dp4_btn_event_cb(lv_event_t *e)
{
  s_touch_count++;
  if (s_touch_count_label) {
    lv_label_set_text_fmt(s_touch_count_label, "Touches: %u", (unsigned)s_touch_count);
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

  lv_obj_t *sub = lv_label_create(scr);
  lv_label_set_text(sub, "D2  -  HAL + MicroTask pump + LEDC backlight");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x8A9099), LV_PART_MAIN);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 62);

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
  lv_label_set_text(s_touch_count_label, "Touches: 0");
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
  : MicroTasks::Task(), _backlightDeadline(0), _backlightOn(false)
{
}

void DisplayP4Task::begin()
{
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

unsigned long DisplayP4Task::loop(MicroTasks::WakeReason reason)
{
  lv_timer_handler();

  unsigned long now = millis();
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
```

- [ ] **Step 3: Update `src/main.cpp`** — replace the D1 call `display_p4_begin();` with `displayP4.begin();` (keep the surrounding `#if defined(ENABLE_SCREEN_LVGL)` guard and the `DBUGF("After display_p4_begin: ...")` line, or rename it to `"After displayP4.begin: ..."`). The include `#include "display_p4/display_p4.h"` stays.

- [ ] **Step 4: Build** — `pio run -e openevse_p4 2>&1 | tail -20`. Expect SUCCESS. Note RAM/Flash.
  - If `ledcAttach`/`ledcWrite` are unresolved: they're core-3 Arduino APIs (this env is core-3) — confirm no typo.
  - If `MicroTask`/`startTask` unresolved: `#include <MicroTasks.h>` is in display_p4.h.

- [ ] **Step 5: Commit** — `git add src/display_p4/display_p4.h src/display_p4/display_p4.cpp src/main.cpp && git commit -m "p4 display: pump LVGL from a MicroTasks::Task; LEDC backlight + idle timeout"`

---

## Task 3: On-hardware verification

- [ ] **Step 1: Flash** — `pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5 2>&1 | tail -8`. Expect `[SUCCESS]`.
- [ ] **Step 2: Visual check (user-assisted).** Confirm with the user:
  1. The bring-up screen still renders correctly (now driven through the HAL + MicroTask pump) and is smooth.
  2. Tapping the button still increments the counter (touch via the cooperative task).
  3. **Backlight idle timeout**: leaving the panel untouched for the timeout window (default 120 s — for a quick test the user may temporarily lower `DISPLAY_P4_BACKLIGHT_TIMEOUT_MS`, e.g. via a build flag, to ~10 s) turns the backlight off; **touching the screen turns it back on**.
  4. The web UI / network remain responsive while the display runs (no starvation) — e.g. the device still answers HTTP if WiFi is provisioned.
- [ ] **Step 3: Record outcome** in memory esp32-p4-port.md (D2 verified).

---

## Self-Review

**Spec coverage (§5.2/§6):** panel/flush HAL seam #3 ✓ (Task 1, `IDisplayPanel`); `lv_timer_handler` pumped from a `MicroTasks::Task` cooperatively ✓ (Task 2 `loop()`); backlight LEDC PWM (GPIO23) + idle timeout ✓ (Task 2). Framebuffer-in-PSRAM and GT911 indev carried from D1.

**Placeholder scan:** none — all new/rewritten files are given in full; `main.cpp` change is a one-line call swap.

**Type consistency:** `IDisplayPanel`/`St7701DsiPanel` (panel.h ↔ panel_st7701_dsi.*), `panel_flush_ready_cb_t` used in both the interface and the task's `dp4_flush_ready_cb`. `DisplayP4Task` declared in display_p4.h, defined in .cpp, `extern displayP4` referenced in main.cpp. `setup()`/`loop()` signatures match the `MicroTasks::Task` base (verified vs `lcd_tft.h`).

**Concurrency note:** LVGL is single-threaded — only the MicroTask `loop()` calls `lv_timer_handler`; the DSI done-callback only calls `lv_disp_flush_ready` (sets a flag) and the touch read_cb sets a `volatile bool`. No LVGL API is called from two contexts. This is safer than D1's raw FreeRTOS task.
