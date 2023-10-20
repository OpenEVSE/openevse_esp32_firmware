#include "lcd_gui.button_bar_png.h"
#include "lcd_gui.logo_png.h"
#include "lcd_gui.not_connected_png.h"
StaticFile lcd_gui_static_files[] = {
  { "/button_bar.png", CONTENT_BUTTON_BAR_PNG, sizeof(CONTENT_BUTTON_BAR_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_BUTTON_BAR_PNG_ETAG, false },
  { "/logo.png", CONTENT_LOGO_PNG, sizeof(CONTENT_LOGO_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_LOGO_PNG_ETAG, false },
  { "/not_connected.png", CONTENT_NOT_CONNECTED_PNG, sizeof(CONTENT_NOT_CONNECTED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_NOT_CONNECTED_PNG_ETAG, false },
};
