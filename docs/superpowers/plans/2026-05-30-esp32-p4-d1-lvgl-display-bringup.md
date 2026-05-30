# P4 Display Bring-Up (D1: ST7701 MIPI-DSI + GT911 touch + LVGL) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Light up the Guition JC4880P443C's 4.3" ST7701S MIPI-DSI panel with GT911 touch under LVGL 8.3.9, running `lv_demo_widgets`, integrated into the `openevse_p4` firmware build and gated behind `-D ENABLE_SCREEN_LVGL`.

**Architecture:** Port the vendor's bundled `esp_lcd` ST7701 (MIPI-DSI) + GT911 touch drivers into `src/display_p4/`, add LVGL 8.3.9 with a project `lv_conf.h`, and a thin glue module that creates the I²C bus, brings up the panel + touch, allocates PSRAM draw buffers, registers the LVGL display + input devices, and pumps `lv_timer_handler` from a dedicated FreeRTOS task. All new code is compiled only when `ENABLE_SCREEN_LVGL` is defined (set only in `[env:openevse_p4]`), matching the existing `ENABLE_SCREEN_LCD_TFT` gating convention (file-body `#ifdef`, no `build_src_filter`).

**Tech Stack:** ESP-IDF 5.5.4 / Arduino-ESP32 3.3.8 (pioarduino core-3), `esp_lcd` MIPI-DSI + `esp_ldo_regulator` (P4 built-ins), LVGL 8.3.9, PlatformIO.

**Hardware facts (from schematic + esphome + vendor demo):**
- Panel ST7701S, 480×800 portrait, MIPI-DSI 2-lane, RGB565. Panel reset = **GPIO5** (hardcoded in vendor `st7701_lcd.cpp`). DSI PHY power = internal **LDO channel 3 @ 2.5 V**. Backlight = **GPIO23** (D1: simple GPIO on; LEDC PWM deferred to D2).
- Touch GT911 on **I²C_NUM_1, SDA=GPIO7 / SCL=GPIO8** (shared bus; the touch driver calls `i2c_master_get_bus_handle(1, …)` so the glue MUST create the I²C_NUM_1 master bus BEFORE `touch.begin()`). Touch RST/INT passed as `-1` (polled), matching the working vendor demo.
- DSI flush uses RGB565 with **no byte-swap** (`LV_COLOR_16_SWAP 0`), unlike the SPI spike.

**Source of truth (vendor demo, do not modify in place — copy from here):**
`/home/rar/oevse/JC4880P443C_I_W/1-Demo/arduino_examples/lvgl_demo_v8/`

---

## File Structure

- Create `src/display_p4/lcd/` — vendored ST7701 esp_lcd driver (7 files, copied).
- Create `src/display_p4/touch/` — vendored GT911 esp_lcd touch driver (6 files, copied).
- Create `src/display_p4/lv_conf.h` — project LVGL 8.3 config (new).
- Create `src/display_p4/display_p4.h` / `display_p4.cpp` — LVGL glue + bring-up + pump task (new).
- Modify `platformio.ini` `[env:openevse_p4]` — add `ENABLE_SCREEN_LVGL`, LVGL lib_dep, include paths.
- Modify `src/main.cpp` — call `display_p4_begin()` in `setup()`, guarded.

All vendored `.c`/`.cpp` files get their body wrapped in `#if defined(ENABLE_SCREEN_LVGL)` … `#endif` so non-P4 envs compile them to nothing (they use P4-only IDF APIs and must not break the 2.x boards). LVGL is added to `lib_deps` only for `openevse_p4`, so other envs never see `lvgl.h`.

**Note on verification:** This is hardware bring-up. `Serial`/USB-CDC debug output is swallowed by HWCDC on this board (only early ROM `ESP_LOG` survives), so per-task "tests" are **build success** + a **final on-hardware visual check** (panel renders the demo; tapping the screen moves/activates demo widgets). The visual check requires the user to look at the panel.

---

## Task 1: Vendor the ST7701 + GT911 drivers into the tree (guarded)

**Files:**
- Create `src/display_p4/lcd/esp_lcd_st7701.c`, `esp_lcd_st7701.h`, `esp_lcd_st7701_interface.h`, `esp_lcd_st7701_mipi.c`, `esp_lcd_st7701_rgb.c`, `st7701_lcd.cpp`, `st7701_lcd.h`
- Create `src/display_p4/touch/esp_lcd_touch.c`, `esp_lcd_touch.h`, `esp_lcd_touch_gt911.c`, `esp_lcd_touch_gt911.h`, `gt911_touch.cpp`, `gt911_touch.h`

- [ ] **Step 1: Copy the vendor driver trees verbatim**

```bash
SRC=/home/rar/oevse/JC4880P443C_I_W/1-Demo/arduino_examples/lvgl_demo_v8/src
DST=/home/rar/oevse/openevse_esp32_firmware/src/display_p4
mkdir -p "$DST/lcd" "$DST/touch"
cp "$SRC"/lcd/*.c "$SRC"/lcd/*.h "$SRC"/lcd/*.cpp "$DST/lcd/"
cp "$SRC"/touch/*.c "$SRC"/touch/*.h "$SRC"/touch/*.cpp "$DST/touch/"
ls "$DST"/lcd "$DST"/touch
```
Expected: 7 files in `lcd/`, 6 files in `touch/`.

- [ ] **Step 2: Wrap each vendored `.c` / `.cpp` body in the build guard**

For each of these 6 implementation files:
`lcd/esp_lcd_st7701.c`, `lcd/esp_lcd_st7701_mipi.c`, `lcd/esp_lcd_st7701_rgb.c`, `lcd/st7701_lcd.cpp`, `touch/esp_lcd_touch.c`, `touch/esp_lcd_touch_gt911.c`, `touch/gt911_touch.cpp`

— insert at the very top (line 1), before any existing `#include`:
```c
#if defined(ENABLE_SCREEN_LVGL)
```
— and append at the very end of the file:
```c
#endif // ENABLE_SCREEN_LVGL
```
Do NOT guard the `.h` headers (they're harmless when not referenced, and the guarded `.c` files include them).

- [ ] **Step 3: Sanity-check the guards**

Run:
```bash
cd /home/rar/oevse/openevse_esp32_firmware
for f in src/display_p4/lcd/esp_lcd_st7701.c src/display_p4/lcd/esp_lcd_st7701_mipi.c src/display_p4/lcd/esp_lcd_st7701_rgb.c src/display_p4/lcd/st7701_lcd.cpp src/display_p4/touch/esp_lcd_touch.c src/display_p4/touch/esp_lcd_touch_gt911.c src/display_p4/touch/gt911_touch.cpp; do
  head -1 "$f" | grep -q "ENABLE_SCREEN_LVGL" && tail -1 "$f" | grep -q "ENABLE_SCREEN_LVGL" && echo "OK  $f" || echo "BAD $f"
done
```
Expected: `OK` for all 7 files.

- [ ] **Step 4: Verify the 2.x build is undisturbed (files present but compiled out)**

Run: `pio run -e openevse_wifi_v1 2>&1 | tail -5`
Expected: `SUCCESS` (these new files exist in `src/` but `ENABLE_SCREEN_LVGL` is undefined for this env, so they compile to nothing; LVGL is not in this env's `lib_deps`).
> If the local 2.x framework was moved aside for core-3 work, restore it first: `mv ~/.platformio/packages/framework-arduinoespressif32.core2x-bak ~/.platformio/packages/framework-arduinoespressif32` (see memory esp32-p4-port). If restoring is disruptive, it is acceptable to rely on CI for the 2.x check and note that here.

- [ ] **Step 5: Commit**

```bash
git add src/display_p4/lcd src/display_p4/touch
git commit -m "p4 display: vendor ST7701 MIPI-DSI + GT911 touch drivers (guarded)"
```

---

## Task 2: Add LVGL 8.3.9 + lv_conf.h + openevse_p4 env wiring

**Files:**
- Create: `src/display_p4/lv_conf.h`
- Modify: `platformio.ini` (`[env:openevse_p4]`)

- [ ] **Step 1: Create `src/display_p4/lv_conf.h`**

```c
// src/display_p4/lv_conf.h — LVGL 8.3 config for the ESP32-P4 ST7701 MIPI-DSI panel.
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// RGB565. DSI panel takes native byte order — NO swap (unlike the SPI/TFT spike).
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// LVGL widget/render scratch pool (internal RAM). Big draw buffers live in PSRAM,
// allocated separately in display_p4.cpp.
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)

// Tick from Arduino millis().
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF 130

// On-screen FPS/CPU + memory instrumentation (useful for the bring-up).
#define LV_USE_PERF_MONITOR 1
#define LV_USE_MEM_MONITOR 1

#define LV_USE_LOG 0
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// Fonts used by lv_demo_widgets.
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// D1 bring-up content: the LVGL widgets demo.
#define LV_USE_DEMO_WIDGETS 1

#endif // LV_CONF_H
```

- [ ] **Step 2: Wire the `[env:openevse_p4]` build flags + lib_deps + include paths**

In `platformio.ini`, inside `[env:openevse_p4]`:

Add to `build_flags` (after the existing `-D DEBUG_PORT=Serial` line):
```ini
  ; --- D1 LVGL display bring-up (ST7701 MIPI-DSI + GT911) ---
  -D ENABLE_SCREEN_LVGL
  -D LV_CONF_INCLUDE_SIMPLE
  -D LV_CONF_SUPPRESS_DEFINE_CHECK
  -I src/display_p4
  -I src/display_p4/lcd
  -I src/display_p4/touch
```

Replace the env's `lib_deps = ${common.lib_deps}` line with:
```ini
lib_deps =
  ${common.lib_deps}
  lvgl/lvgl@^8.3.9
```

- [ ] **Step 3: Build to shake out compilation + IDF API drift**

Run: `pio run -e openevse_p4 2>&1 | tail -40`
Expected: ideally `SUCCESS`. The vendor drivers targeted IDF 5.3; we are on 5.5.4, so minor `esp_lcd` MIPI-DSI / `esp_ldo` signature drift is possible.
> If there are compile errors in the vendored `.c` files, fix them minimally against the installed IDF headers (do NOT rewrite the drivers). Likely spots: `esp_lcd_dpi_panel_config_t` field names, `esp_ldo_channel_config_t` fields, `esp_lcd_panel_io_i2c_config_t`. Look up the real struct in `~/.platformio/packages/framework-arduinoespressif32/tools/esp32-arduino-libs/esp32p4/include/...` and adjust field-by-field. Record each change in the commit message.
> If `lvgl.h` / `lv_conf.h` is not found: confirm `-D LV_CONF_INCLUDE_SIMPLE` and `-I src/display_p4` are present, and that `lv_conf.h` is at `src/display_p4/lv_conf.h`.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini src/display_p4/lv_conf.h
git commit -m "p4 display: add LVGL 8.3.9 + lv_conf, gate openevse_p4 on ENABLE_SCREEN_LVGL"
```
(If Step 3 required driver edits, `git add src/display_p4/lcd src/display_p4/touch` too and describe the IDF-drift fixes.)

---

## Task 3: LVGL glue — bring-up + pump task — and main.cpp hook

**Files:**
- Create: `src/display_p4/display_p4.h`, `src/display_p4/display_p4.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Create `src/display_p4/display_p4.h`**

```cpp
#ifndef DISPLAY_P4_H
#define DISPLAY_P4_H

#if defined(ENABLE_SCREEN_LVGL)

// Brings up the ST7701 MIPI-DSI panel + GT911 touch + LVGL, loads the LVGL
// widgets demo, and starts a FreeRTOS task that pumps lv_timer_handler.
// Call once from setup(), after PSRAM/heap are available.
void display_p4_begin();

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_H
```

- [ ] **Step 2: Create `src/display_p4/display_p4.cpp`** (glue adapted from the vendor `lvgl_demo_v8.ino`, with the I²C-bus-first ordering, no color swap, and a dedicated pump task)

```cpp
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
```
> If Task 2's build revealed that `esp_lcd_dpi_panel_event_callbacks_t` / `esp_lcd_dpi_panel_register_event_callbacks` have different names/signatures on IDF 5.5.4, match them to the installed header (`esp_lcd_mipi_dsi.h`). The `={}` initializers are intentional (zero-init) to tolerate field additions.

- [ ] **Step 3: Hook into `src/main.cpp` `setup()`**

Add the include near the other display includes at the top of `src/main.cpp`:
```cpp
#if defined(ENABLE_SCREEN_LVGL)
#include "display_p4/display_p4.h"
#endif
```
Then, in `setup()`, immediately AFTER the existing `lcd.begin(evse, scheduler, manual);` line (currently line 148) and its `DBUGF` line, add:
```cpp
#if defined(ENABLE_SCREEN_LVGL)
  display_p4_begin();
  DBUGF("After display_p4_begin: %d", ESPAL.getFreeHeap());
#endif
```

- [ ] **Step 4: Build**

Run: `pio run -e openevse_p4 2>&1 | tail -25`
Expected: `SUCCESS`. Note flash/RAM usage in the output (PSRAM buffers are heap, not static, so static RAM should barely move; flash grows by LVGL + demo).
> If LVGL demo symbols (`lv_demo_widgets`) are unresolved, confirm `LV_USE_DEMO_WIDGETS 1` is in `lv_conf.h` and `#include "demos/lv_demos.h"` resolves (LVGL ships demos under its `demos/` dir; the lib_dep includes them).

- [ ] **Step 5: Commit**

```bash
git add src/display_p4/display_p4.h src/display_p4/display_p4.cpp src/main.cpp
git commit -m "p4 display: LVGL glue + pump task; run lv_demo_widgets from setup()"
```

---

## Task 4: On-hardware bring-up verification

**Files:** none (flash + observe).

- [ ] **Step 1: Flash to the P4**

Run: `pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5 2>&1 | tail -8`
Expected: `[SUCCESS]`, `Hash of data verified`, hard reset.

- [ ] **Step 2: Visual verification (user-assisted)**

Ask the user to look at the 4.3" panel and confirm:
1. Backlight is on and the panel shows the **LVGL widgets demo** (tabs, charts, sliders, a meter) — not garbage/tearing, correct colors, **480×800 portrait** orientation.
2. The on-screen **FPS/CPU** (top/bottom corner, from `LV_USE_PERF_MONITOR`) updates.
3. **Tapping** the screen activates widgets (e.g. switching demo tabs, moving a slider) and the touch point lands where they tapped (no large offset / inverted axis).

- [ ] **Step 3: Record outcome**

If all three pass → D1 complete; update memory esp32-p4-port.md with "P4 panel + touch + LVGL verified on hardware (D1)".
If colors are wrong → revisit `LV_COLOR_16_SWAP` / `rgb_ele_order`.
If touch axes are inverted/swapped vs. portrait → adjust `s_touch.set_rotation(...)` or the GT911 `swap_xy/mirror_x/mirror_y` flags in the vendor `gt911_touch.cpp` config, rebuild, recheck.
If the panel is blank but build/flash succeeded → most likely DSI timing (`ST7701_480_360_PANEL_60HZ_DPI_CONFIG`) or LDO sequencing; compare the vendor demo's exact init order and confirm the LDO channel (3) and reset GPIO (5).

---

## Self-Review

**Spec coverage (vs design §5.2):** ST7701 MIPI-DSI bring-up ✓ (Task 1–3), DSI PHY LDO ch3 2.5 V ✓ (vendor driver), framebuffer in PSRAM ✓ (Task 3 step 2), LVGL 8.x pinned to shared minor ✓ (8.3.9, Task 2), flush bound to DSI panel ✓ (Task 3), GT911 → LVGL indev ✓ (Task 3). Backlight LEDC PWM + idle timeout and the `MicroTasks::Task` pump are **explicitly deferred to D2** (this plan uses simple GPIO backlight + a FreeRTOS task) — noted, not dropped. Portrait OpenEVSE screens + `IEvseUiModel` are D3/D4.

**Placeholder scan:** none — all new files have full content; vendored files are copied verbatim then guard-wrapped; the IDF-drift fallback in Task 2/3 gives concrete remediation locations rather than "fix errors."

**Type consistency:** `display_p4_begin()` (declared in `.h`, defined in `.cpp`, called in `main.cpp`) — consistent. `bsp_lcd_handles_t`, `st7701_lcd`, `gt911_touch` come from the vendored headers. `lv_color_t`, `lv_disp_*`, `lv_indev_*` from LVGL 8.3.

**Known risk carried into execution:** IDF 5.3→5.5.4 `esp_lcd` MIPI-DSI / `esp_ldo` API drift (Task 2 Step 3, Task 3 Step 4). Mitigated by minimal field-level fixes against installed headers, not driver rewrites.
