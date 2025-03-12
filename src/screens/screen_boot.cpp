#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "screens/screen_boot.h"
#include "lcd_common.h"

unsigned long BootScreen::update()
{
  if(_full_update)
  {
    _screen.fillScreen(TFT_OPENEVSE_BACK);
    _screen.fillSmoothRoundRect(90, 60, 300, 110, 15, TFT_WHITE);
    render_image("/logo.png", 104, 85, _screen);
    _full_update = false;
  }

  TFT_eSprite sprite(&_screen);
  uint16_t *pixels = (uint16_t *)sprite.createSprite(BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT);
  if(nullptr == pixels)
  {
    DBUGF("Failed to create sprite for boot progress %d x %d", BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT);
    return 166;
  }
  sprite.fillSprite(TFT_OPENEVSE_BACK);
  sprite.fillRoundRect(0, 0, BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT, 8, TFT_WHITE);
  if(_boot_progress > 0) {
    sprite.fillRoundRect(0, 0, _boot_progress, BOOT_PROGRESS_HEIGHT, 8, TFT_OPENEVSE_GREEN);
  }
  _screen.startWrite();
  _screen.pushImage(BOOT_PROGRESS_X, BOOT_PROGRESS_Y, BOOT_PROGRESS_WIDTH, BOOT_PROGRESS_HEIGHT, pixels);
  _screen.endWrite();
  sprite.deleteSprite();
  _boot_progress += 10;

  // Display any message lines
  String line = get_message_line(0);
  if(line.length() > 0) {
    render_centered_text_box(line.c_str(), 0, 250, TFT_SCREEN_WIDTH, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, !_full_update, 1, _screen);
  }
  line = get_message_line(1);
  if(line.length() > 0) {
    render_centered_text_box(line.c_str(), 0, 270, TFT_SCREEN_WIDTH, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, !_full_update, 1, _screen);
  }

  return 166;
}
