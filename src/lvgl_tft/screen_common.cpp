#include "screen_common.h"

#ifdef ENABLE_SCREEN_LVGL_TFT

#include "openevse.h"     // OPENEVSE_STATE_*
#include "nightshift.h"   // NS_* palette macros

const char *state_word(uint8_t s, lv_color_t *colour)
{
  switch (s) {
    case OPENEVSE_STATE_CHARGING:      *colour = NS_SUCCESS; return "CHARGING";
    case OPENEVSE_STATE_CONNECTED:     *colour = NS_ACCENT;  return "CONNECTED";
    case OPENEVSE_STATE_SLEEPING:      *colour = NS_SLEEP;   return "SLEEPING";
    case OPENEVSE_STATE_DISABLED:      *colour = NS_TEXTDIM; return "DISABLED";
    case OPENEVSE_STATE_STARTING:      *colour = NS_ACCENT;  return "STARTING";
    case OPENEVSE_STATE_NOT_CONNECTED: *colour = NS_TEXTDIM; return "NOT CONNECTED";
    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:  *colour = NS_ERROR;   return "FAULT";
    default:                           *colour = NS_TEXTDIM; return "--";
  }
}

int wifi_percent(int rssi)
{
  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;
  return 2 * (rssi + 100);
}

#endif // ENABLE_SCREEN_LVGL_TFT
