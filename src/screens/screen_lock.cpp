#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#include "emonesp.h"
#include "screens/screen_lock.h"
#include "screens/screen_renderer.h"
#include "lcd_common.h"

#define LOCK_ICON_X             ((TFT_SCREEN_WIDTH - 48) / 2)
#define LOCK_ICON_Y             100
#define LOCK_MESSAGE_WIDTH      (TFT_SCREEN_WIDTH - 60)
#define LOCK_MESSAGE_X          ((TFT_SCREEN_WIDTH - LOCK_MESSAGE_WIDTH) / 2)
#define LOCK_MESSAGE_Y          200

LockScreen::LockScreen(TFT_eSPI &screen, EvseManager &evse, Scheduler &scheduler, ManualOverride &manual) :
  ScreenBase(screen, evse, scheduler, manual),
  _lockMessage(LOCK_SCREEN_MESSAGE)
{
}

void LockScreen::init()
{
  ScreenBase::init();
}

unsigned long LockScreen::update()
{
  if (_full_update)
  {
    // Clear the entire screen with background color
    _screen.fillScreen(TFT_OPENEVSE_BACK);

    // Draw a white rounded rectangle in the center of the screen
    const uint16_t rect_x = 20;
    const uint16_t rect_y = 60;
    const uint16_t rect_width = TFT_SCREEN_WIDTH - 40;
    const uint16_t rect_height = TFT_SCREEN_HEIGHT - 120;
    _screen.fillSmoothRoundRect(rect_x, rect_y, rect_width, rect_height, 10, TFT_WHITE);

    // Draw lock icon from PNG (if available) or draw a simple lock shape
    // Attempt to render the lock image from a PNG file at the specified location
    render_image("/lock.png", LOCK_ICON_X, LOCK_ICON_Y, _screen);

    // Always draw the text message
    render_centered_text_box(_lockMessage,
                            LOCK_MESSAGE_X,
                            LOCK_MESSAGE_Y,
                            LOCK_MESSAGE_WIDTH,
                            &FreeSans9pt7b,
                            TFT_OPENEVSE_TEXT,
                            TFT_WHITE,
                            false,
                            2,
                            _screen);
  }

  // Draw the date/time at the top
  char buffer[32];
  timeval local_time;
  gettimeofday(&local_time, NULL);
  struct tm timeinfo;
  localtime_r(&local_time.tv_sec, &timeinfo);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d  %H:%M:%S", &timeinfo);
  render_left_text_box(buffer, 12, 30, 175, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1, _screen);

  _full_update = false;

  // Update every second to keep the time current
  return 1000;
}

#endif // ENABLE_SCREEN_LCD_TFT
