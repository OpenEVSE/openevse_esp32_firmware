#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#include "emonesp.h"
#include "screens/screen_charge.h"
#include "lcd_common.h"
#include <WiFi.h>
#include <sys/time.h>

#define BUTTON_BAR_X            0
#define BUTTON_BAR_Y            (TFT_SCREEN_HEIGHT - BUTTON_BAR_HEIGHT)
#define BUTTON_BAR_HEIGHT       55
#define BUTTON_BAR_WIDTH        TFT_SCREEN_WIDTH

#define DISPLAY_AREA_X          0
#define DISPLAY_AREA_Y          0
#define DISPLAY_AREA_WIDTH      TFT_SCREEN_WIDTH
#define DISPLAY_AREA_HEIGHT     (TFT_SCREEN_HEIGHT - BUTTON_BAR_HEIGHT)

#define WHITE_AREA_BOARDER      8
#define WHITE_AREA_X            WHITE_AREA_BOARDER
#define WHITE_AREA_Y            45
#define WHITE_AREA_WIDTH        (DISPLAY_AREA_WIDTH - (2 * 8))
#define WHITE_AREA_HEIGHT       (DISPLAY_AREA_HEIGHT - (WHITE_AREA_Y + 20))

#define INFO_BOX_BOARDER        8
#define INFO_BOX_X              ((WHITE_AREA_X + WHITE_AREA_WIDTH) - (INFO_BOX_WIDTH + INFO_BOX_BOARDER))
#define INFO_BOX_WIDTH          190
#define INFO_BOX_HEIGHT         56

void ChargeScreen::init()
{
  ScreenBase::init();
  _previous_evse_state = _evse.getEvseState();
}

bool ChargeScreen::setWifiMode(bool client, bool connected)
{
  if (client != wifi_client || connected != wifi_connected) {
    wifi_client = client;
    wifi_connected = connected;
    return true;
  }

  return false;
}

unsigned long ChargeScreen::update()
{
  uint8_t evse_state = _evse.getEvseState();

  //redraw when going in or out of charging state to switch between pilot and power display
  if ((evse_state == OPENEVSE_STATE_CHARGING || _previous_evse_state == OPENEVSE_STATE_CHARGING)
      && evse_state != _previous_evse_state) {
    _full_update = true;
  }

  if(_full_update)
  {
    _screen.fillRect(DISPLAY_AREA_X, DISPLAY_AREA_Y, DISPLAY_AREA_WIDTH, DISPLAY_AREA_HEIGHT, TFT_OPENEVSE_BACK);
    _screen.fillSmoothRoundRect(WHITE_AREA_X, WHITE_AREA_Y, WHITE_AREA_WIDTH, WHITE_AREA_HEIGHT, 6, TFT_WHITE);
    render_image("/button_bar.png", BUTTON_BAR_X, BUTTON_BAR_Y, _screen);
  }

  String status_icon = "/disabled.png";
  String car_icon = "/car_disconnected.png";
  String wifi_icon = "/no_wifi.png";

  if(_evse.isVehicleConnected()) {
    car_icon = "/car_connected.png";
  }

  switch (evse_state)
  {
    case OPENEVSE_STATE_STARTING:
      status_icon = "/start.png";
      break;
    case OPENEVSE_STATE_NOT_CONNECTED:
      status_icon = "/not_connected.png";
      break;
    case OPENEVSE_STATE_CONNECTED:
      status_icon = "/connected.png";
      break;
    case OPENEVSE_STATE_CHARGING:
      status_icon = "/charging.png";
      break;
    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:
      status_icon = "/error.png";
      break;
    case OPENEVSE_STATE_SLEEPING:
      status_icon = "/sleeping.png";
      break;
    case OPENEVSE_STATE_DISABLED:
      status_icon = "/disabled.png";
      break;
    default:
      break;
  }

  char buffer[32] = "";
  char buffer2[12];

  if (wifi_client) {
    if (wifi_connected) {
      wifi_icon = "/wifi.png";
      snprintf(buffer, sizeof(buffer), "%ddB", WiFi.RSSI());
    }
  } else {
    if (wifi_connected) {
      wifi_icon = "/access_point_connected.png";
      snprintf(buffer, sizeof(buffer), "%d", WiFi.softAPgetStationNum());
    } else {
      wifi_icon = "/access_point.png";
    }
  }
  render_right_text_box(buffer, 350, 30, 50, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1, _screen);

  render_image(status_icon.c_str(), 16, 52, _screen);
  render_image(car_icon.c_str(), 16, 92, _screen);
  render_image(wifi_icon.c_str(), 16, 132, _screen);

  if (evse_state == OPENEVSE_STATE_CHARGING) {
    float power = _evse.getPower() / 1000.0;  //kW
    if (power < 10) {
      snprintf(buffer, sizeof(buffer), "%.2f", power);
    } else if (power < 100) {
      snprintf(buffer, sizeof(buffer), "%.1f", power);
    } else {
      snprintf(buffer, sizeof(buffer), "%.0f", power);
    }
    render_left_text_box(buffer, 66, 157, 188, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, !_full_update, 2, _screen);
    render_left_text_box("kW", 224, 165, 34, &FreeSans9pt7b, TFT_BLACK, TFT_WHITE, false, 1, _screen);
  } else {
    snprintf(buffer, sizeof(buffer), "%d", _evse.getChargeCurrent());
    render_right_text_box(buffer, 66, 175, 154, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, !_full_update, 2, _screen);
    if (_full_update) {
      render_left_text_box("A", 224, 165, 34, &FreeSans24pt7b, TFT_BLACK, TFT_WHITE, false, 1, _screen);
    }
  }
  if (_evse.isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR)) {
    snprintf(buffer, sizeof(buffer), "%.1fC", _evse.getTemperature(EVSE_MONITOR_TEMP_MONITOR));
    render_right_text_box(buffer, 415, 30, 50, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1, _screen);
  }

  snprintf(buffer, sizeof(buffer), "%.1f V  %.2f A", _evse.getVoltage(), _evse.getAmps());
  if (evse_state == OPENEVSE_STATE_CHARGING) {
    snprintf(buffer2, sizeof(buffer2), "Pilot: %dA", _evse.getChargeCurrent());
  } else {
    get_scaled_number_value(_evse.getPower(), 2, "W", buffer2, sizeof(buffer2));
  }
  render_data_box(buffer2, buffer, 66, 175, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update, _screen);

  String line = get_message_line(0);
  if(line.length() == 0) {
    line = esp_hostname;
  }
  render_centered_text_box(line.c_str(), INFO_BOX_X, 74, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT, TFT_WHITE, !_full_update, 1, _screen);

  line = get_message_line(1);
  render_centered_text_box(line.c_str(), INFO_BOX_X, 96, INFO_BOX_WIDTH, &FreeSans9pt7b, TFT_OPENEVSE_TEXT, TFT_WHITE, !_full_update, 1, _screen);

  uint32_t elapsed = _evse.getSessionElapsed();
  uint32_t hours = elapsed / 3600;
  uint32_t minutes = (elapsed % 3600) / 60;
  uint32_t seconds = elapsed % 60;
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
  render_info_box("ELAPSED", buffer, INFO_BOX_X, 110, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update, _screen);

  get_scaled_number_value(_evse.getSessionEnergy(), 0, "Wh", buffer, sizeof(buffer));
  render_info_box("DELIVERED", buffer, INFO_BOX_X, 175, INFO_BOX_WIDTH, INFO_BOX_HEIGHT, _full_update, _screen);

  timeval local_time;
  gettimeofday(&local_time, NULL);
  struct tm timeinfo;
  localtime_r(&local_time.tv_sec, &timeinfo);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d  %H:%M:%S", &timeinfo);
  render_left_text_box(buffer, 12, 30, 175, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_BACK, false, 1, _screen);

  _previous_evse_state = evse_state;

  //sleep until next whole second so clock doesn't skip
  gettimeofday(&local_time, NULL);
  unsigned long nextUpdate = 1000 - local_time.tv_usec/1000;
  _full_update = false;

  return nextUpdate;
}

#endif // ENABLE_SCREEN_LCD_TFT
