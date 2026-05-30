#ifndef DISPLAY_P4_H
#define DISPLAY_P4_H

#if defined(ENABLE_SCREEN_LVGL)

// Brings up the ST7701 MIPI-DSI panel + GT911 touch + LVGL, loads the LVGL
// widgets demo, and starts a FreeRTOS task that pumps lv_timer_handler.
// Call once from setup(), after PSRAM/heap are available.
void display_p4_begin();

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_H
