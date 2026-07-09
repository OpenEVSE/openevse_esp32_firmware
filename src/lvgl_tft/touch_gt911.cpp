// src/lvgl_tft/touch_gt911.cpp — see touch_gt911.h.
#if defined(ENABLE_TOUCH_GT911) && defined(ENABLE_SCREEN_LVGL_TFT)

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "touch_gt911.h"

// I2C address is strapped by the level of INT during reset; driving INT low
// (as the init sequence below does) selects 0x5D.
#ifndef TOUCH_GT911_ADDR
#define TOUCH_GT911_ADDR 0x5D
#endif
#ifndef TOUCH_GT911_I2C_FREQ
#define TOUCH_GT911_I2C_FREQ 400000
#endif

// Raw -> screen coordinate mapping. The GT911 reports in the panel's native
// portrait frame (TFT_WIDTH x TFT_HEIGHT); the LVGL display runs landscape
// (TFT_HEIGHT x TFT_WIDTH). Mirrors are applied AFTER the swap, on the final
// screen axes. Pairing with the panel rotation:
//   TFT_ROTATION=3 -> SWAP_XY=1 MIRROR_X=1 MIRROR_Y=0  (default)
//   TFT_ROTATION=1 -> SWAP_XY=1 MIRROR_X=0 MIRROR_Y=1
#ifndef TOUCH_GT911_SWAP_XY
#define TOUCH_GT911_SWAP_XY 1
#endif
#ifndef TOUCH_GT911_MIRROR_X
#define TOUCH_GT911_MIRROR_X 1
#endif
#ifndef TOUCH_GT911_MIRROR_Y
#define TOUCH_GT911_MIRROR_Y 0
#endif

#define GT911_REG_PRODUCT_ID 0x8140  // 4 ASCII bytes, "911"
#define GT911_REG_STATUS     0x814E  // bit7 = frame ready, bits0-3 = touch count
#define GT911_REG_POINT0     0x814F  // track id, x lo/hi, y lo/hi, size lo/hi, ...

// Landscape screen size (matches lvgl_panel.cpp).
static const int32_t SCREEN_W = TFT_HEIGHT; // 480
static const int32_t SCREEN_H = TFT_WIDTH;  // 320

static bool s_ok = false;             // controller answered at init
static bool s_pressed = false;        // current press state
static bool s_touched_since = false;  // any press since last was_touched()
static lv_coord_t s_last_x = 0;
static lv_coord_t s_last_y = 0;
static lv_indev_drv_t s_indev_drv;

static bool gt911_read(uint16_t reg, uint8_t *buf, size_t len)
{
  Wire.beginTransmission(TOUCH_GT911_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xff));
  if(Wire.endTransmission(false) != 0) {
    return false;
  }
  // uint8_t args pick an overload that exists on both Arduino core 2.x and 3.x
  if(Wire.requestFrom((uint8_t)TOUCH_GT911_ADDR, (uint8_t)len) != len) {
    return false;
  }
  for(size_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

static bool gt911_write8(uint16_t reg, uint8_t val)
{
  Wire.beginTransmission(TOUCH_GT911_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xff));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static void gt911_lvgl_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  uint8_t status;
  if(s_ok && gt911_read(GT911_REG_STATUS, &status, 1) && (status & 0x80))
  {
    uint8_t count = status & 0x0f;
    if(count > 0)
    {
      uint8_t p[5]; // track id, x lo, x hi, y lo, y hi (first point only)
      if(gt911_read(GT911_REG_POINT0, p, sizeof(p)))
      {
        int32_t x = (int32_t)p[1] | ((int32_t)p[2] << 8);
        int32_t y = (int32_t)p[3] | ((int32_t)p[4] << 8);
#if TOUCH_GT911_SWAP_XY
        int32_t t = x; x = y; y = t;
#endif
#if TOUCH_GT911_MIRROR_X
        x = (SCREEN_W - 1) - x;
#endif
#if TOUCH_GT911_MIRROR_Y
        y = (SCREEN_H - 1) - y;
#endif
        if(x < 0) x = 0; else if(x >= SCREEN_W) x = SCREEN_W - 1;
        if(y < 0) y = 0; else if(y >= SCREEN_H) y = SCREEN_H - 1;
        s_last_x = (lv_coord_t)x;
        s_last_y = (lv_coord_t)y;
        s_pressed = true;
        s_touched_since = true;
      }
    } else {
      s_pressed = false;
    }
    gt911_write8(GT911_REG_STATUS, 0); // ack the frame
  }
  // No fresh frame: report the previous state (the GT911 refreshes every few
  // ms, far faster than LVGL polls, so a held finger never looks released).
  data->point.x = s_last_x;
  data->point.y = s_last_y;
  data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool touch_gt911_init()
{
  // Address-select reset: INT held low through reset selects address 0x5D.
  pinMode(TOUCH_GT911_INT, OUTPUT);
  digitalWrite(TOUCH_GT911_INT, LOW);
  pinMode(TOUCH_GT911_RST, OUTPUT);
  digitalWrite(TOUCH_GT911_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_GT911_RST, HIGH);
  delay(5);
  pinMode(TOUCH_GT911_INT, INPUT); // release; polled over I2C, not via INT
  delay(50);

  Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL, (uint32_t)TOUCH_GT911_I2C_FREQ);

  uint8_t id[4] = {0};
  s_ok = gt911_read(GT911_REG_PRODUCT_ID, id, sizeof(id));
  if(!s_ok) {
    Serial.println("[touch] GT911 not responding; touch disabled");
    return false;
  }
  Serial.printf("[touch] GT911 up, product id '%c%c%c'\n", id[0], id[1], id[2]);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = gt911_lvgl_read;
  lv_indev_drv_register(&s_indev_drv);
  return true;
}

bool touch_gt911_was_touched()
{
  bool ret = s_touched_since;
  s_touched_since = false;
  return ret;
}

#endif // ENABLE_TOUCH_GT911 && ENABLE_SCREEN_LVGL_TFT
