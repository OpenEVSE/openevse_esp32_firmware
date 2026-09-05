#ifndef __LCD_TFT_H
#define __LCD_TFT_H

#include "screens/screen_manager.h"

#include <TFT_eSPI.h>
#include <PNGdec.h>

#define LCD_MAX_LINES 2

class LcdTask : public LcdTaskBase
{
  private:
    TFT_eSPI _tft;                  // The TFT display

#ifdef ENABLE_DOUBLE_BUFFER
    TFT_eSprite _back_buffer;       // The back buffer
    uint16_t *_back_buffer_pixels;
#endif

    TFT_eSPI &_screen;                 // What we are going to write to

    // The TFT screen is portrait natively, so we need to rotate it
    const uint16_t _screen_width  = TFT_HEIGHT;
    const uint16_t _screen_height = TFT_WIDTH;

    bool _initialise = true;

    // Screen management
    ScreenManager* _screenManager = nullptr;

    void backlightControl(bool on) override;
    void onMessageDisplayed(Message *msg) override;

  protected:
    void setup() override;
    unsigned long loop(MicroTasks::WakeReason reason) override;

  public:
    LcdTask();
    ~LcdTask() override;

    void begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) override;

    void setWifiMode(bool client, bool connected) override;

    void fill_screen(uint16_t color) {
      _screen.fillScreen(color);
    }
};

extern char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1]; // Message buffer shared with renderers

#endif // __LCD_TFT_H
