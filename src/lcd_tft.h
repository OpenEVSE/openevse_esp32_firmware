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

//#include <lvgl.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>

class LcdTask : public MicroTasks::Task
{
  private:
    TFT_eSPI _lcd;

    // The TFT screen is portrate natively, so we need to rotate it
    const uint16_t _screenWidth  = TFT_HEIGHT;
    const uint16_t _screenHeight = TFT_WIDTH;

    enum class State {
      Boot,
      Charge
    };

    State _state = State::Boot;

    static void png_draw(PNGDRAW *pDraw);
  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

    void render_image(const char *filename, int16_t x, int16_t y);
    void load_font(const char *filename);

  public:
    LcdTask();

    void begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual);

    void display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
    void display(String &msg, int x, int y, int time, uint32_t flags);
    void display(const char *msg, int x, int y, int time, uint32_t flags);

    void fill_screen(uint16_t color) {
      _lcd.fillScreen(color);
    }
};

extern LcdTask lcd;

#endif // __LCD_TFT_H
