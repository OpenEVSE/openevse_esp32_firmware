// src/lvgl_tft/touch_gt911.h — GT911 capacitive touch as an LVGL pointer
// input device. Self-contained Wire driver (registers 0x814E/0x814F), no
// external library. Selected by ENABLE_TOUCH_GT911; pins come from the
// TOUCH_GT911_SDA/SCL/INT/RST build flags, coordinate mapping from
// TOUCH_GT911_SWAP_XY / _MIRROR_X / _MIRROR_Y (defaults pair with
// TFT_ROTATION=3 on the Elecrow CrowPanel Advance 3.5").
#ifndef __TOUCH_GT911_H
#define __TOUCH_GT911_H

#ifdef ENABLE_TOUCH_GT911

// Bring up I2C + the controller and register the LVGL input device. Call
// after the LVGL display driver is registered (lvgl_panel_begin does this).
// Returns false if the controller doesn't answer; the display keeps working.
bool touch_gt911_init();

// True if any press was seen since the last call (clear-on-read). LcdTask
// polls this to wake the backlight / leave the standby screen on touch.
bool touch_gt911_was_touched();

#endif // ENABLE_TOUCH_GT911
#endif // __TOUCH_GT911_H
