#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <Arduino.h>
#include <WiFi.h>
#include <MicroTasks.h>
#include <lvgl.h>
#include <sys/time.h>

#include "emonesp.h"
#include "lcd.h"
#include "openevse.h"
#include "espal.h"
#include "app_config.h"   // esp_hostname, tft_theme
#include "lvgl_tft/lvgl_panel.h"
#include "lvgl_tft/nightshift.h"
#include "lvgl_tft/boot_screen.h"
#include "lvgl_tft/setup_screen.h"
#include "lvgl_tft/charge_screen.h"
#include "lvgl_tft/standby_screen.h"
#include "lvgl_tft/backlight.h"

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN TFT_BL
#endif

// How long the startup splash shows before handing off to the main screen.
#define BOOT_SPLASH_MS 4000

// _activeScreen values.
#define SCR_BOOT   0
#define SCR_SETUP  1
#define SCR_CHARGE 2
#define SCR_STANDBY 3

#ifdef EPOXY_DUINO
static uint32_t g_lvgl_last_tick = 0;

static void lvgl_pump()
{
  uint32_t now = millis();
  lv_tick_inc(now - g_lvgl_last_tick);
  g_lvgl_last_tick = now;
  lv_timer_handler();
  lvgl_panel_pump();
}
#else
static void lvgl_pump()
{
  lv_timer_handler();
}
#endif

// --- Message inner class (mechanics identical to the TFT_eSPI LcdTask) ---

LcdTask::Message::Message(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags) :
  _next(NULL), _msg(""), _x(x), _y(y), _time(time), _clear(flags & LCD_CLEAR_LINE ? 1 : 0)
{
  strncpy_P(_msg, reinterpret_cast<PGM_P>(msg), LCD_MAX_LEN);
  _msg[LCD_MAX_LEN] = '\0';
}

LcdTask::Message::Message(String &msg, int x, int y, int time, uint32_t flags) :
  Message(msg.c_str(), x, y, time, flags)
{
}

LcdTask::Message::Message(const char *msg, int x, int y, int time, uint32_t flags) :
  _next(NULL), _msg(""), _x(x), _y(y), _time(time), _clear(flags & LCD_CLEAR_LINE ? 1 : 0)
{
  strncpy(_msg, msg, LCD_MAX_LEN);
  _msg[LCD_MAX_LEN] = '\0';
}

LcdTask::LcdTask() :
  MicroTasks::Task(),
  _head(NULL),
  _tail(NULL),
  _nextMessageTime(0),
  _evse(NULL)
{
  clearMessageLines();
}

void LcdTask::clearMessageLines()
{
  for (int i = 0; i < LCD_MAX_LINES; i++) {
    _msg[i][0] = '\0';
  }
  _msg_cleared = true;
}

void LcdTask::display(Message *msg, uint32_t flags)
{
  if(flags & LCD_DISPLAY_NOW) {
    for(Message *next, *node = _head; node; node = next) {
      next = node->getNext();
      delete node;
    }
    _head = NULL;
    _tail = NULL;
  }

  if(_tail) {
    _tail->setNext(msg);
  } else {
    _head = msg;
    _nextMessageTime = millis();
  }
  _tail = msg;

  if(flags & LCD_DISPLAY_NOW) {
    displayNextMessage();
  }
}

void LcdTask::display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags)
{
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::display(String &msg, int x, int y, int time, uint32_t flags)
{
  display(new Message(msg, x, y, time, flags), flags);
}

void LcdTask::display(const char *msg, int x, int y, int time, uint32_t flags)
{
  display(new Message(msg, x, y, time, flags), flags);
}

unsigned long LcdTask::displayNextMessage()
{
  while(_head && millis() >= _nextMessageTime)
  {
    Message *msg = _head;
    _head = _head->getNext();
    if(NULL == _head) {
      _tail = NULL;
    }

    wakeBacklight();

    int line = msg->getY();
    if(line >= 0 && line < LCD_MAX_LINES) {
      strncpy(_msg[line], msg->getMsg(), LCD_MAX_LEN);
      _msg[line][LCD_MAX_LEN] = '\0';
      _msg_cleared = false;
    }

    _nextMessageTime = millis() + msg->getTime();
    delete msg;
  }

  return _nextMessageTime - millis();
}

void LcdTask::setWifiMode(bool client, bool connected)
{
  _wifi_client = client;
  _wifi_connected = connected;
  _wifiModeKnown = true;
  wakeBacklight();
}

// Resolve the tft_theme config into the active palette. Returns true if the theme
// actually changed (so the caller can rebuild the on-screen widgets to repaint).
bool LcdTask::applyThemeFromConfig()
{
  int want = tft_theme.equals("light") ? 1 : 0;  // anything not "light" => dark
  if(want == _themeLight) {
    return false;
  }
  _themeLight = want;
  ns_set_theme(want == 1);
  return true;
}

void LcdTask::buildSetupScreen()
{
  String ssid = WiFi.softAPSSID();
  if(ssid.length() == 0) {
    ssid = esp_hostname;
  }
  // Matches net_manager: configured ap_pass if >= 8 chars, else the default.
  const char *pass = (ap_pass.length() >= 8) ? ap_pass.c_str() : "openevse";
  String ip = WiFi.softAPIP().toString();

  // WiFi-join payload a phone camera recognises (defaults have no chars needing
  // WIFI:-format escaping; a custom ap_ssid/ap_pass with ; , : \ would).
  char qr[160];
  snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", ssid.c_str(), pass);
  setup_screen_build(qr, ssid.c_str(), pass, ip.c_str());
}

void LcdTask::begin(EvseManager &evse, Scheduler &scheduler, ManualOverride &manual)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void LcdTask::setup()
{
}

unsigned long LcdTask::loop(MicroTasks::WakeReason reason)
{
  if(_initialise)
  {
    // Bring up LVGL + the panel AFTER networking (it breaks the display if done
    // earlier — same constraint the TFT_eSPI renderer documents).
    DBUGVAR(ESP.getFreeHeap());
    _displayOk = lvgl_panel_begin();
    if(_displayOk) {
      applyThemeFromConfig();  // pick the palette before the first screen is built
      applyDisplayConfig();    // cache brightness/timeout before the first wake
      boot_screen_build();
      _booting = true;
      _bootStart = millis();
#ifdef EPOXY_DUINO
      g_lvgl_last_tick = _bootStart;
#endif
      // lvgl_panel_begin() already owns the backlight pin via LEDC; a raw
      // pinMode here would detach the PWM binding on core 3.x (perimanager).
      wakeBacklight();
    }
    _initialise = false;
  }

  if(!_displayOk) {
    return 5000; // no panel registered; nothing to draw
  }

  // Drain queued messages into the message lines, clear them when their time is up.
  if(_head) {
    displayNextMessage();
  }
  if(!_msg_cleared && millis() >= _nextMessageTime) {
    clearMessageLines();
  }

  // Combined transient message string (shown by both the boot splash and charge screen).
  char ml[2 * LCD_MAX_LEN + 4];
  ml[0] = '\0';
  if(!_msg_cleared) {
    if(_msg[0][0] && _msg[1][0])      snprintf(ml, sizeof(ml), "%s  %s", _msg[0], _msg[1]);
    else if(_msg[0][0])               snprintf(ml, sizeof(ml), "%s", _msg[0]);
    else if(_msg[1][0])               snprintf(ml, sizeof(ml), "%s", _msg[1]);
  }

  // Boot splash: fill the progress bar over BOOT_SPLASH_MS, then hand off to the
  // charge screen (build it, load it, delete the splash).
  if(_booting) {
    uint32_t el = millis() - _bootStart;
    boot_screen_update((int)(el * 100 / BOOT_SPLASH_MS), ml);
    lvgl_pump();
    if(el >= BOOT_SPLASH_MS) {
      // Show the QR setup screen only if we KNOW we're in AP mode; otherwise the
      // charge screen (default), and switch later if AP mode is reported.
      if(_wifiModeKnown && !_wifi_client) {
        buildSetupScreen();
        _activeScreen = SCR_SETUP;
      } else {
        charge_screen_build();
        _activeScreen = SCR_CHARGE;
      }
      boot_screen_destroy();
      _booting = false;
      lvgl_pump();
      return 50;
    }
    return 120;
  }

  // Switch screens if the WiFi mode resolved differently than what's showing
  // (e.g. AP -> STA once the user completes setup via the QR).
  bool wantSetup = _wifiModeKnown && !_wifi_client;
  // The setup screen owns the whole display; never run standby in AP mode.
  if(wantSetup && _standby) {
    wakeBacklight();  // exits standby, rebuilds the charge screen, so the swap below works
  }
  if(wantSetup && _activeScreen == SCR_CHARGE) {
    buildSetupScreen();
    charge_screen_destroy();
    _activeScreen = SCR_SETUP;
  } else if(!wantSetup && _activeScreen == SCR_SETUP) {
    charge_screen_build();
    setup_screen_destroy();
    _activeScreen = SCR_CHARGE;
  }

  // Live theme switch: swap palette + rebuild whichever screen is showing.
  if(applyThemeFromConfig()) {
    if(_activeScreen == SCR_CHARGE) {
      charge_screen_build();
    } else if(_activeScreen == SCR_SETUP) {
      buildSetupScreen();
    } else if(_activeScreen == SCR_STANDBY) {
      standby_screen_build();
    }
    lvgl_pump();
  }

  // The setup screen is static — just pump LVGL and idle.
  if(_activeScreen == SCR_SETUP) {
    lvgl_pump();
    return 1000;
  }

  // --- Backlight / standby decision (chooses which screen we render) ---
  uint8_t state = _evse->getEvseState();
  bool vehicle = _evse->isVehicleConnected();
  applyDisplayConfig();   // pick up live /config changes; also applies brightness now
                          // so slider changes take effect without waiting for a wake

  if(_prev_state != state || _prev_vehicle != vehicle) {
    wakeBacklight();      // any state change -> full brightness, exit standby, re-arm
    _prev_state = state;
    _prev_vehicle = vehicle;
  }

  bool keepAwake = stateKeepsAwake(state, vehicle, _evse->getAmps());
  if(keepAwake) {
    _lastWake = millis(); // keep re-arming so we never time out while charging/fault
    if(_standby) {
      wakeBacklight();
    }
  }
  // bl_should_standby returns false when keepAwake, so this is a plain guard.
  if(bl_should_standby(keepAwake, (uint32_t)_timeoutS, millis() - _lastWake)) {
    if(!_standby) {
      enterStandby();
    }
  }

  // Render the standby screen when dimmed-with-screen; otherwise fall through to charge.
  if(_activeScreen == SCR_STANDBY) {
    StandbyScreenData sd = {};
    sd.evse_state        = state;
    sd.temp_valid        = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR);
    sd.temp_c            = sd.temp_valid ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0.0f;
    sd.temp_fahrenheit   = temp_unit.equals("f");
    sd.wifi_client       = _wifi_client;
    sd.wifi_connected    = _wifi_connected;
    sd.rssi              = WiFi.RSSI();
    sd.sta_count         = WiFi.softAPgetStationNum();
    sd.today_kwh         = _evse->getTotalDay();
    sd.total_kwh         = _evse->getTotalEnergy();

    char ck[24];
    timeval tv; gettimeofday(&tv, NULL);
    struct tm ti; localtime_r(&tv.tv_sec, &ti);
    strftime(ck, sizeof(ck), "%Y-%m-%d  %H:%M:%S", &ti);  // match the charge screen header
    sd.clock = ck;

    char ipbuf[20];
    IPAddress ip = _wifi_client ? WiFi.localIP() : WiFi.softAPIP();
    snprintf(ipbuf, sizeof(ipbuf), "%s", ip.toString().c_str());
    sd.hostname = esp_hostname.c_str();
    sd.ip = ipbuf;

    standby_screen_update(sd);
    lvgl_pump();
    gettimeofday(&tv, NULL);
    return 1000 - tv.tv_usec / 1000;
  }

  // Assemble a full snapshot from EvseManager + WiFi + clock.
  ChargeScreenData d = {};
  d.evse_state        = state;
  d.charging          = (state == OPENEVSE_STATE_CHARGING);
  d.vehicle_connected = vehicle;
  d.power_kw          = _evse->getPower() / 1000.0f;
  d.pilot_a           = (int)_evse->getChargeCurrent();
  d.volts             = _evse->getVoltage();
  d.amps              = _evse->getAmps();
  d.elapsed_s         = _evse->getSessionElapsed();
  d.session_wh        = _evse->getSessionEnergy();
  d.temp_valid        = _evse->isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR);
  d.temp_c            = d.temp_valid ? _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) : 0.0f;
  d.temp_fahrenheit   = temp_unit.equals("f");
  d.wifi_client       = _wifi_client;
  d.wifi_connected    = _wifi_connected;
  d.rssi              = WiFi.RSSI();
  d.sta_count         = WiFi.softAPgetStationNum();

  char dt[24];
  timeval tv;
  gettimeofday(&tv, NULL);
  struct tm ti;
  localtime_r(&tv.tv_sec, &ti);
  strftime(dt, sizeof(dt), "%Y-%m-%d  %H:%M:%S", &ti);
  d.datetime = dt;

  // Bottom row: hostname (left) + IP (right). A transient message overrides both.
  char ipbuf[20];
  IPAddress ip = _wifi_client ? WiFi.localIP() : WiFi.softAPIP();
  snprintf(ipbuf, sizeof(ipbuf), "%s", ip.toString().c_str());
  d.hostname = esp_hostname.c_str();
  d.ip = ipbuf;
  d.msg_line = (!_msg_cleared && ml[0]) ? ml : "";

  charge_screen_update(d);
  lvgl_pump();

  // Wake on the next whole second so the clock doesn't skip.
  gettimeofday(&tv, NULL);
  return 1000 - tv.tv_usec / 1000;
}

void LcdTask::applyDisplayConfig()
{
  _activeBrightness  = (int32_t)tft_brightness;
  _standbyBrightness = (int32_t)tft_standby_brightness;
  _timeoutS          = (int32_t)lcd_backlight_timeout;
  if(_activeBrightness < 10) _activeBrightness = 10;  // never black out the active screen
  // Apply live so brightness-slider changes take effect without waiting for a wake.
  lvgl_panel_set_backlight((uint8_t)(_standby ? (_standbyBrightness < 0 ? 0 : _standbyBrightness)
                                              : (_activeBrightness  < 0 ? 100 : _activeBrightness)));
}

void LcdTask::wakeBacklight()
{
  _lastWake = millis();
  if(_activeBrightness < 0) _activeBrightness = (int32_t)tft_brightness;  // boot safety
  lvgl_panel_set_backlight((uint8_t)_activeBrightness);
  if(_standby) {
    _standby = false;
    if(_activeScreen == SCR_STANDBY) {
      charge_screen_build();
      standby_screen_destroy();
      _activeScreen = SCR_CHARGE;
    }
  }
}

void LcdTask::enterStandby()
{
  _standby = true;
  if(_standbyBrightness > 0) {
    standby_screen_build();
    if(_activeScreen == SCR_CHARGE) {
      charge_screen_destroy();
    }
    _activeScreen = SCR_STANDBY;
  }
  lvgl_panel_set_backlight((uint8_t)(_standbyBrightness < 0 ? 0 : _standbyBrightness));
}

bool LcdTask::stateKeepsAwake(uint8_t state, bool vehicle, double amps)
{
  if(!vehicle) {
    return false;
  }
  switch(state) {
    case OPENEVSE_STATE_STARTING:
    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:
      return true;
    case OPENEVSE_STATE_CHARGING:
#ifdef TFT_BACKLIGHT_CHARGING_THRESHOLD
      return amps >= TFT_BACKLIGHT_CHARGING_THRESHOLD;
#else
      return true;
#endif
    default:
      return false;
  }
}

LcdTask lcd;

#endif // ENABLE_SCREEN_LVGL_TFT
