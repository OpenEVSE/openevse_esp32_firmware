#include "lcd_gui.charge_svg.h"
#include "lcd_gui.logo_png.h"
StaticFile lcd_gui_static_files[] = {
  { "/charge.svg", CONTENT_CHARGE_SVG, sizeof(CONTENT_CHARGE_SVG) - 1, _CONTENT_TYPE_SVG, CONTENT_CHARGE_SVG_ETAG, false },
  { "/logo.png", CONTENT_LOGO_PNG, sizeof(CONTENT_LOGO_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_LOGO_PNG_ETAG, false },
};
