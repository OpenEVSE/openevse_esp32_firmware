#include "lcd_gui.access_point_png.h"
#include "lcd_gui.access_point_connected_png.h"
#include "lcd_gui.button_bar_png.h"
#include "lcd_gui.car_connected_png.h"
#include "lcd_gui.car_disconnected_png.h"
#include "lcd_gui.charging_png.h"
#include "lcd_gui.connected_png.h"
#include "lcd_gui.disabled_png.h"
#include "lcd_gui.error_png.h"
#include "lcd_gui.logo_png.h"
#include "lcd_gui.no_wifi_png.h"
#include "lcd_gui.not_connected_png.h"
#include "lcd_gui.sleeping_png.h"
#include "lcd_gui.start_png.h"
#include "lcd_gui.wifi_png.h"
StaticFile lcd_gui_static_files[] = {
  { "/access_point.png", CONTENT_ACCESS_POINT_PNG, sizeof(CONTENT_ACCESS_POINT_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_ACCESS_POINT_PNG_ETAG, false },
  { "/access_point_connected.png", CONTENT_ACCESS_POINT_CONNECTED_PNG, sizeof(CONTENT_ACCESS_POINT_CONNECTED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_ACCESS_POINT_CONNECTED_PNG_ETAG, false },
  { "/button_bar.png", CONTENT_BUTTON_BAR_PNG, sizeof(CONTENT_BUTTON_BAR_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_BUTTON_BAR_PNG_ETAG, false },
  { "/car_connected.png", CONTENT_CAR_CONNECTED_PNG, sizeof(CONTENT_CAR_CONNECTED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_CAR_CONNECTED_PNG_ETAG, false },
  { "/car_disconnected.png", CONTENT_CAR_DISCONNECTED_PNG, sizeof(CONTENT_CAR_DISCONNECTED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_CAR_DISCONNECTED_PNG_ETAG, false },
  { "/charging.png", CONTENT_CHARGING_PNG, sizeof(CONTENT_CHARGING_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_CHARGING_PNG_ETAG, false },
  { "/connected.png", CONTENT_CONNECTED_PNG, sizeof(CONTENT_CONNECTED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_CONNECTED_PNG_ETAG, false },
  { "/disabled.png", CONTENT_DISABLED_PNG, sizeof(CONTENT_DISABLED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_DISABLED_PNG_ETAG, false },
  { "/error.png", CONTENT_ERROR_PNG, sizeof(CONTENT_ERROR_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_ERROR_PNG_ETAG, false },
  { "/logo.png", CONTENT_LOGO_PNG, sizeof(CONTENT_LOGO_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_LOGO_PNG_ETAG, false },
  { "/no_wifi.png", CONTENT_NO_WIFI_PNG, sizeof(CONTENT_NO_WIFI_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_NO_WIFI_PNG_ETAG, false },
  { "/not_connected.png", CONTENT_NOT_CONNECTED_PNG, sizeof(CONTENT_NOT_CONNECTED_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_NOT_CONNECTED_PNG_ETAG, false },
  { "/sleeping.png", CONTENT_SLEEPING_PNG, sizeof(CONTENT_SLEEPING_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_SLEEPING_PNG_ETAG, false },
  { "/start.png", CONTENT_START_PNG, sizeof(CONTENT_START_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_START_PNG_ETAG, false },
  { "/wifi.png", CONTENT_WIFI_PNG, sizeof(CONTENT_WIFI_PNG) - 1, _CONTENT_TYPE_PNG, CONTENT_WIFI_PNG_ETAG, false },
};
