#include "lcd_gui.BootScreen_png.h"
#include "lcd_gui.ChargeScreen_png.h"
StaticFile lcd_gui_static_files[] = {
  { "/BootScreen.png", CONTENT_BOOTSCREEN_PNG, sizeof(CONTENT_BOOTSCREEN_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_BOOTSCREEN_PNG_ETAG, false },
  { "/ChargeScreen.png", CONTENT_CHARGESCREEN_PNG, sizeof(CONTENT_CHARGESCREEN_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_CHARGESCREEN_PNG_ETAG, false },
};
