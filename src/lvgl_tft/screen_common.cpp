// src/lvgl_tft/screen_common.cpp — see screen_common.h.
#include "screen_common.h"

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <stdio.h>        // snprintf
#include <string.h>       // strcmp
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

// --- Write-if-changed wrappers (see screen_common.h for why) ----------------

void ui_set_text(lv_obj_t *label, const char *text)
{
  if (NULL == label || NULL == text) return;
  const char *cur = lv_label_get_text(label);
  if (NULL != cur && 0 == strcmp(cur, text)) return;
  lv_label_set_text(label, text);
}

void ui_set_text_color(lv_obj_t *obj, lv_color_t colour)
{
  if (NULL == obj) return;
  if (lv_obj_get_style_text_color(obj, LV_PART_MAIN).full == colour.full) return;
  lv_obj_set_style_text_color(obj, colour, 0);
}

void ui_set_bg_color(lv_obj_t *obj, lv_color_t colour)
{
  if (NULL == obj) return;
  if (lv_obj_get_style_bg_color(obj, LV_PART_MAIN).full == colour.full) return;
  lv_obj_set_style_bg_color(obj, colour, 0);
}

void ui_set_arc_color(lv_obj_t *obj, lv_color_t colour, lv_style_selector_t selector)
{
  if (NULL == obj) return;
  if (lv_obj_get_style_arc_color(obj, selector).full == colour.full) return;
  lv_obj_set_style_arc_color(obj, colour, selector);
}

void ui_set_font(lv_obj_t *obj, const lv_font_t *font)
{
  if (NULL == obj || NULL == font) return;
  if (lv_obj_get_style_text_font(obj, LV_PART_MAIN) == font) return;
  lv_obj_set_style_text_font(obj, font, 0);
}

void ui_set_border_width(lv_obj_t *obj, lv_coord_t width)
{
  if (NULL == obj) return;
  if (lv_obj_get_style_border_width(obj, LV_PART_MAIN) == width) return;
  lv_obj_set_style_border_width(obj, width, 0);
}

void ui_set_hidden(lv_obj_t *obj, bool hidden)
{
  if (NULL == obj) return;
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

#endif // ENABLE_SCREEN_LVGL_TFT
