#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN TFT_BL
#endif

#include "emonesp.h"
#include "lcd.h"
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"
#include "app_config.h"
#include <sys/time.h>
#include "embedded_files.h"
//#include "fonts/DejaVu_Sans_72.h"
#include <SvgParser.h>

#define TFT_OPENEVSE_BACK   0x2413
#define TFT_OPENEVSE_GREEN  0x3E92

#include "web_server.h"

PNG png;

#include "lcd_static/lcd_gui_static_files.h"

#define MAX_IMAGE_WIDTH TFT_HEIGHT // Adjust for your images

struct image_render_state {
  TFT_eSPI *tft;
  int16_t xpos;
  int16_t ypos;
};

LcdTask::LcdTask() :
  MicroTasks::Task(),
  _lcd(),
  _state(State::Boot),
  _svgOutput(_lcd),
  _svg(&_svgOutput)
{
}

void LcdTask::display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags)
{
}

void LcdTask::display(String &msg, int x, int y, int time, uint32_t flags)
{
}

void LcdTask::display(const char *msg, int x, int y, int time, uint32_t flags)
{
}

void LcdTask::begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual)
{
  MicroTask.startTask(this);
}

void LcdTask::setup()
{
}

unsigned long LcdTask::loop(MicroTasks::WakeReason reason)
{
  DBUG("LCD UI woke: ");
  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");

  unsigned long nextUpdate = MicroTask.Infinate;

  switch(_state)
  {
    case State::Boot:
      DBUGLN("LCD UI setup");

      _lcd.begin();
      _lcd.setRotation(1);

      _lcd.fillScreen(TFT_OPENEVSE_BACK);
      _lcd.fillSmoothRoundRect(90, 90, 300, 110, 15, TFT_WHITE);
      _lcd.fillSmoothRoundRect(90, 235, 300, 16, 8, TFT_OPENEVSE_GREEN);
      render_image("/logo.png", 104, 115);

      pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
      digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
      nextUpdate = 5000;
      _state = State::Charge;
      break;

    case State::Charge:
//      render_image("/ChargeScreen.png", 0, 0);
//
//      _lcd.setCursor(120, 180);
//      _lcd.setTextColor(TFT_BLACK, TFT_WHITE);
////      _lcd.setFreeFont(&DejaVu_Sans_72);
////      _lcd.setTextSize(1);
//      _lcd.setFreeFont(&FreeSans24pt7b);
//      _lcd.setTextSize(3);
//      _lcd.print("16");
//      _lcd.setFreeFont(&FreeSans24pt7b);
//      _lcd.setTextSize(1);
//      _lcd.println("A");
      render_svg("/charge.svg", 0, 0);

      break;

    default:
      break;
  }

  DBUGVAR(nextUpdate);
  return nextUpdate;
}

void LcdTask::render_image(const char *filename, int16_t x, int16_t y)
{
  StaticFile *file = NULL;
  if(embedded_get_file(filename, lcd_gui_static_files, ARRAY_LENGTH(lcd_gui_static_files), &file))
  {
    DBUGF("Found %s (%d bytes)", filename, file->length);
    int16_t rc = png.openFLASH((uint8_t *)file->data, file->length, png_draw);
    if (rc == PNG_SUCCESS)
    {
      DBUGLN("Successfully opened png file");
      DBUGF("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
      _lcd.startWrite();
      uint32_t dt = millis();
      image_render_state state = {&_lcd, x, y};
      rc = png.decode(&state, 0);
      DBUG(millis() - dt); DBUGLN("ms");
      _lcd.endWrite();
      // png.close(); // not needed for memory->memory decode
    }
  }
}

void LcdTask::render_svg(const char *filename, int16_t x, int16_t y)
{
  StaticFile *file = NULL;
  if(embedded_get_file(filename, lcd_gui_static_files, ARRAY_LENGTH(lcd_gui_static_files), &file))
  {
    DBUGF("Found %s (%d bytes)", filename, file->length);
    DBUGF("SVG: %p %p\n", _svg, &_svg);
    _svg.readFlash((const uint8_t *)file->data, file->length);
    _svg.print(x, y);
  }
}

#ifdef SMOOTH_FONT
void LcdTask::load_font(const char *filename)
{
  StaticFile *file = NULL;
  if(embedded_get_file(filename, lcd_gui_static_files, ARRAY_LENGTH(lcd_gui_static_files), &file))
  {
    DBUGF("Found %s (%d bytes)", filename, file->length);
    _lcd.loadFont((uint8_t *)file->data);
  }
}
#endif

void LcdTask::png_draw(PNGDRAW *pDraw)
{
  image_render_state *state = (image_render_state *)pDraw->pUser;
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  state->tft->pushImage(state->xpos, state->ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}

void LcdTask::SvgOutput_TFT_eSPI::circle(int16_t x, int16_t y, int16_t radius, struct svgStyle_t * style)
{
    DBUGF("CIRCLE: x %i y %i radiuss %i stroke width: %i\n",x,y,radius,style->stroke_width);
    tft.drawCircle(x, y, radius, style->stroke_color);
    int16_t start=0, end; // radius is in middle of stroke

    if(style->stroke_color_set != UNSET && style->stroke_width) {
        uint16_t stroke_color = convertColor(style->stroke_color);

        start = style->stroke_width*style->x_scale/2;
        end = start;
        end += (style->stroke_width*style->x_scale -start -end);

        // check if this can be done by printing to filled circles
        if(style->fill_color_set == SET){
            start = style->stroke_width*style->x_scale;
            tft.fillCircle(x, y, radius+end, stroke_color);
        } else {

            DBUGF("start: %i end: %i\n",start,end);
            for(uint16_t i=radius-start; i<radius+end; i++)
              tft.drawCircle(x, y, i, stroke_color);
        }
    }

    // filled circle?
    if(style->fill_color_set == SET) {
        tft.fillCircle(x, y, radius-start, convertColor(style->fill_color));
    }

}

void LcdTask::SvgOutput_TFT_eSPI::rect(int16_t x, int16_t y, int16_t width, int16_t height, struct svgStyle_t * style)
{
    DBUGF("RECT: x %i y %i width %i height %i\n",x,y,width, height);

    // filled rect?
    if(style->fill_color_set == SET) {
        tft.fillRect(x, y, width, height, convertColor(style->fill_color));
    }

    if(style->stroke_color_set != UNSET && style->stroke_width) {
        uint16_t stroke_color = convertColor(style->stroke_color);
        tft.drawRect(x, y, width, height, stroke_color);
    }
}
void LcdTask::SvgOutput_TFT_eSPI::text(int16_t x, int16_t y, char * text, struct svgStyle_t * style)
{
    DBUGF("TEXT: x %i y %i size %i text \"%s\"\n", x, y, style->font_size, text);
    if(style->stroke_color_set == UNSET && style->fill_color_set == UNSET) return;

    tft.setTextColor(convertColor(style->stroke_color));
            tft.setTextSize(1);

    DBUGF("cur font height: %i\n",tft.fontHeight());
    uint8_t newHeight = round(style->font_size*style->y_scale/ tft.fontHeight());
    if(!newHeight) newHeight++;
            DBUGF("height factor: %i\n",newHeight);

    tft.setTextSize(newHeight);

    tft.setTextDatum(BL_DATUM);
    tft.drawString(text, x, y + (style->font_size*style->y_scale - tft.fontHeight())/2);

}

void LcdTask::SvgOutput_TFT_eSPI::path(uint16_t *data, uint16_t len, struct svgStyle_t * style)
{
    if(len<2) return;
    if(style->stroke_color_set == UNSET) return;
    uint16_t color = convertColor(style->stroke_color);

    for(uint16_t i = 1; i<len; i++)
        tft.drawLine(data[(i-1)*2], data[(i-1)*2+1], data[i*2], data[i*2 + 1], color);

    DBUGF("PATH: len: %i \n",len);
}


LcdTask lcd;

#endif // ENABLE_SCREEN_LCD_lcd
