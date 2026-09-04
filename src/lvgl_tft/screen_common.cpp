// src/lvgl_tft/screen_common.cpp — see screen_common.h.
#include "screen_common.h"

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdio.h>        // snprintf
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
    // Every fault names itself, continuing what PP MISSING/SHORTED started. A
    // bare "FAULT" tells whoever is standing at the charger nothing about what
    // to do next, and these want different responses: a GFCI trip is a reset,
    // no ground is an electrician, over-temp is airflow.
    case OPENEVSE_STATE_VENT_REQUIRED:        *colour = NS_ERROR; return "VENT REQUIRED";
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:   *colour = NS_ERROR; return "DIODE CHECK";
    case OPENEVSE_STATE_GFI_FAULT:            *colour = NS_ERROR; return "GFCI TRIP";
    case OPENEVSE_STATE_NO_EARTH_GROUND:      *colour = NS_ERROR; return "NO GROUND";
    case OPENEVSE_STATE_STUCK_RELAY:          *colour = NS_ERROR; return "STUCK RELAY";
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED: *colour = NS_ERROR; return "GFCI SELF TEST";
    case OPENEVSE_STATE_OVER_TEMPERATURE:     *colour = NS_ERROR; return "OVER TEMP";
    case OPENEVSE_STATE_OVER_CURRENT:         *colour = NS_ERROR; return "OVER CURRENT";
    case OPENEVSE_STATE_RELAY_CLOSURE_FAULT:  *colour = NS_ERROR; return "RELAY FAULT";
    case OPENEVSE_STATE_EEPROM_FAILURE:       *colour = NS_ERROR; return "EEPROM FAIL";
    case OPENEVSE_STATE_PP_MISSING:           *colour = NS_ERROR; return "PP MISSING";
    case OPENEVSE_STATE_PP_SHORTED:           *colour = NS_ERROR; return "PP SHORTED";
    default:                                  *colour = NS_TEXTDIM; return "--";
  }
}

int wifi_percent(int rssi)
{
  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;
  return 2 * (rssi + 100);
}

int fmt_temp(char *buf, size_t n, float temp_c, bool fahrenheit)
{
  float t = fahrenheit ? (temp_c * 9.0f / 5.0f + 32.0f) : temp_c;
  return snprintf(buf, n, "%.1f%c  ", t, fahrenheit ? 'F' : 'C');
}

#endif // ENABLE_SCREEN_LVGL_TFT
