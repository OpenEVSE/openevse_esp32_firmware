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
#include <SvgParser.h>

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

    State _state;

    class SvgOutput_TFT_eSPI : public SvgOutput
    {
      private :
        TFT_eSPI &tft;

      public:
        SvgOutput_TFT_eSPI(TFT_eSPI &tft) :
          tft(tft) {};

        virtual void circle(int16_t x, int16_t y, int16_t radius, struct svgStyle_t * style);
        virtual void rect(int16_t x, int16_t y, int16_t width, int16_t height, struct svgStyle_t * style);
        virtual void text(int16_t x, int16_t y, char * text, struct svgStyle_t * style);
        virtual void path(uint16_t *data, uint16_t len, struct svgStyle_t * style);

      private:
        uint16_t convertColor(uint32_t color){
          return tft.color565((color & 0x00FF0000) >> 16, (color & 0x0000FF00) >> 8, color & 0x000000FF);
        }
    } _svgOutput;

    SvgParser _svg;

    static void png_draw(PNGDRAW *pDraw);
  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

    void render_image(const char *filename, int16_t x, int16_t y);
    void render_svg(const char *filename, int16_t x, int16_t y);
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
