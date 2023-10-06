#ifndef __LCD_TFT_H
#define __LCD_TFT_H

#define LCD_CHAR_STOP       1
#define LCD_CHAR_PLAY       2
#define LCD_CHAR_LIGHTNING  3
#define LCD_CHAR_LOCK       4
#define LCD_CHAR_CLOCK      5

#include "evse_man.h"
#include "scheduler.h"
#include "manual.h"

#include <lvgl.h>
#include <TFT_eSPI.h>


class LcdTask : public MicroTasks::Task
{
  private:
    TFT_eSPI _lcd;

    // The TFT screen is portrate natively, so we need to rotate it
    const uint16_t _screenWidth  = TFT_HEIGHT;
    const uint16_t _screenHeight = TFT_WIDTH;

    lv_disp_draw_buf_t _draw_buf;
    lv_color_t *_buf1;

    static void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    LcdTask();

    void begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);

    void display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
    void display(String &msg, int x, int y, int time, uint32_t flags);
    void display(const char *msg, int x, int y, int time, uint32_t flags);
};

extern LcdTask lcd;

#endif // __LCD_TFT_H
