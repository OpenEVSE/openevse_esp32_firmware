#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LED)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <openevse.h>

#ifdef ESP32
#include <analogWrite.h>
#endif

#include "debug.h"
#include "emonesp.h"
#include "LedManagerTask.h"

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH)

#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEO_PIXEL_LENGTH, NEO_PIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

#define FADE_STEP         16
#define FADE_DELAY        50

#define CONNECTING_FLASH_TIME 450
#define CONNECTED_FLASH_TIME  250

#define TEST_LED_TIME     500

#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)

// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix
const uint8_t PROGMEM gamma8[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
  2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
 10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
 25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
 37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
 51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
 69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
 90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

#endif

#ifdef WIFI_BUTTON

#if defined(WIFI_LED) && WIFI_LED == WIFI_BUTTON
#define WIFI_BUTTON_SHARE_LED WIFI_LED
#elif defined(RED_LED) && RED_LED == WIFI_BUTTON
#define WIFI_BUTTON_SHARE_LED RED_LED
#elif defined(GREEN_LED) && GREEN_LED == WIFI_BUTTON
#define WIFI_BUTTON_SHARE_LED GREEN_LED
#elif defined(BLUE_LED) && BLUE_LED == WIFI_BUTTON
#define WIFI_BUTTON_SHARE_LED BLUE_LED
#endif

#endif

#ifdef WIFI_BUTTON_SHARE_LED
uint8_t buttonShareState = 0;
#endif

LedManagerTask::LedManagerTask() :
  MicroTasks::Task(),
  state(LedState_Test_Red),
  evseState(OPENEVSE_STATE_STARTING),
  brightness(LED_DEFAULT_BRIGHTNESS)
{
}

void LedManagerTask::setup()
{
#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH)
  DBUGF("Initialising NeoPixels");
  strip.begin();
  //strip.setBrightness(brightness);
  setAllRGB(0, 0, 0);
#endif

#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)
  DBUGF("Initialising RGB LEDs, %d, %d, %d", RED_LED, GREEN_LED, BLUE_LED);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW);
#endif

#ifdef WIFI_LED
  DBUGF("Configuring pin %d for LED", WIFI_LED);
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, !WIFI_LED_ON_STATE);
#endif

#if defined(WIFI_BUTTON) && !defined(WIFI_BUTTON_SHARE_LED)
  DBUGF("Configuring pin %d for Button", WIFI_BUTTON);
  pinMode(WIFI_BUTTON, WIFI_BUTTON_PRESSED_PIN_MODE);
#endif
}

unsigned long LedManagerTask::loop(MicroTasks::WakeReason reason)
{
  DBUG("LED manager woke: ");
  DBUG(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
       WakeReason_Event == reason ? "WakeReason_Event" :
       WakeReason_Message == reason ? "WakeReason_Message" :
       WakeReason_Manual == reason ? "WakeReason_Manual" :
       "UNKNOWN");
  DBUG(" ");
  DBUGLN(LedState_Off == state ? "LedState_Off" :
         LedState_Test_Red == state ? "LedState_Test_Red" :
         LedState_Test_Green == state ? "LedState_Test_Green" :
         LedState_Test_Blue == state ? "LedState_Test_Blue" :
         LedState_Unknown == state ? "LedState_Unknown" :
         LedState_Ready == state ? "LedState_Ready" :
         LedState_Connected == state ? "LedState_Connected" :
         LedState_Charging == state ? "LedState_Charging" :
         LedState_Sleeping == state ? "LedState_Sleeping" :
         LedState_Warning == state ? "LedState_Warning" :
         LedState_Error == state ? "LedState_Error" :
         LedState_WiFi_Access_Point_Waiting == state ? "LedState_WiFi_Access_Point_Waiting" :
         LedState_WiFi_Access_Point_Connected == state ? "LedState_WiFi_Access_Point_Connected" :
         LedState_WiFi_Client_Connected == state ? "LedState_WiFi_Client_Connected" :
         "UNKNOWN");

#if RGB_LED
  switch(state)
  {
    case LedState_Off:
      setAllRGB(0, 0, 0);
      return MicroTask.Infinate;

    case LedState_Test_Red:
      setAllRGB(255, 0, 0);
      state = LedState_Test_Green;
      return TEST_LED_TIME;

    case LedState_Test_Green:
      setAllRGB(0, 255, 0);
      state = LedState_Test_Blue;
      return TEST_LED_TIME;

    case LedState_Test_Blue:
      setAllRGB(0, 0, 255);
      state = LedState_Off;
      setNewState(false);
      return TEST_LED_TIME;

    case LedState_Unknown:
      setAllRGB(255, 255, 255);
      return MicroTask.Infinate;

    case LedState_Ready:
      setAllRGB(0, 255, 0);
      return MicroTask.Infinate;

    case LedState_Connected:
      setAllRGB(255, 255, 0);
      return MicroTask.Infinate;

    case LedState_Charging:
      setAllRGB(0, 255, 255);
      return MicroTask.Infinate;

    case LedState_Sleeping:
      setAllRGB(255, 0, 255);
      return MicroTask.Infinate;

    case LedState_Warning:
      setAllRGB(255, 255, 0);
      return MicroTask.Infinate;

    case LedState_Error:
      setAllRGB(255, 0, 0);
      return MicroTask.Infinate;

    case LedState_WiFi_Access_Point_Waiting:
      setAllRGB(flashState ? 255 : 0,
                flashState ? 255 : 0,
                0);
      flashState = !flashState;
      return CONNECTING_FLASH_TIME;

    case LedState_WiFi_Access_Point_Connected:
      setAllRGB(0, flashState ? 255 : 0, 0);
      flashState = !flashState;
      return CONNECTED_FLASH_TIME;

    case LedState_WiFi_Client_Connecting:
      setAllRGB(flashState ? 255 : 0,
                flashState ? 255 : 0,
                0);
      flashState = !flashState;
      return CONNECTING_FLASH_TIME;

    case LedState_WiFi_Client_Connected:
      setAllRGB(0, 255, 0);
      return MicroTask.Infinate;

  }
#endif

#ifdef WIFI_LED
  switch(state)
  {
    case LedState_Test_Red:
    case LedState_Test_Green:
    case LedState_Test_Blue:
      setWiFiLed(WIFI_LED_ON_STATE);
      state = LedState_Off;
      setNewState(false);
      return TEST_LED_TIME;

    case LedState_Off:
      setWiFiLed(!WIFI_LED_ON_STATE);
      return MicroTask.Infinate;

    case LedState_WiFi_Access_Point_Waiting:
      setWiFiLed(flashState ? WIFI_LED_ON_STATE : !WIFI_LED_ON_STATE);
      flashState = !flashState;
      return CONNECTING_FLASH_TIME;

    case LedState_WiFi_Access_Point_Connected:
      setWiFiLed(flashState ? WIFI_LED_ON_STATE : !WIFI_LED_ON_STATE);
      flashState = !flashState;
      return CONNECTED_FLASH_TIME;

    case LedState_WiFi_Client_Connecting:
      setWiFiLed(flashState ? WIFI_LED_ON_STATE : !WIFI_LED_ON_STATE);
      flashState = !flashState;
      return CONNECTING_FLASH_TIME;

    case LedState_WiFi_Client_Connected:
      setWiFiLed(WIFI_LED_ON_STATE);
      return MicroTask.Infinate;

    default:
      break;
  }
#endif

  return MicroTask.Infinate;
}

/*
int LedManagerTask::fadeLed(int fadeValue, int FadeDir)
{
  fadeValue += FadeDir;
  if(fadeValue >= MAX_BM_FADE_LED)
  {
    fadeValue = MAX_BM_FADE_LED;
    FadeDir = -FADE_STEP;
  } else if(fadeValue <= 0) {
    fadeValue = 0;
    FadeDir = FADE_STEP;
  }
  return fadeValue;
}
*/

#if RGB_LED
void LedManagerTask::setAllRGB(uint8_t red, uint8_t green, uint8_t blue)
{
  DBUG("LED R:");
  DBUG(red);
  DBUG(" G:");
  DBUG(green);
  DBUG(" B:");
  DBUGLN(blue);

  if(brightness) { // See notes in setBrightness()
    red = (red * brightness) >> 8;
    green = (green * brightness) >> 8;
    blue = (blue * brightness) >> 8;
  }

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH)
  uint32_t col = strip.gamma32(strip.Color(red, green, blue));
  DBUGVAR(col, HEX);
  strip.fill(0, strip.numPixels(), col);
  strip.show();
#endif

#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)
  analogWrite(RED_LED, pgm_read_byte(&gamma8[red]));
  analogWrite(GREEN_LED, pgm_read_byte(&gamma8[green]));
  analogWrite(BLUE_LED, pgm_read_byte(&gamma8[blue]));

#ifdef WIFI_BUTTON_SHARE_LED
  #if RED_LED == WIFI_BUTTON_SHARE_LED
    buttonShareState = red;
  #elif GREEN_LED == WIFI_BUTTON_SHARE_LED
    buttonShareState = green;
  #elif BLUE_LED == WIFI_BUTTON_SHARE_LED
    buttonShareState = blue;
  #endif
#endif
#endif
}
#endif

#ifdef WIFI_LED
void LedManagerTask::setWiFiLed(uint8_t state)
{
  DBUGVAR(state);
  digitalWrite(WIFI_LED, state);  // Stored brightness value is different than what's passed.
  // This simplifies the actual scaling math later, allowing a fast
  // 8x8-bit multiply and taking the MSB. 'brightness' is a uint8_t,
  // adding 1 here may (intentionally) roll over...so 0 = max brightness
  // (color values are interpreted ltimingiterally; no scaling), 1 = min
  // brightness (off), 255 = just below max brightness.

#ifdef WIFI_BUTTON_SHARE_LED
  buttonShareState = state ? 0 : 255;
#endif
}
#endif

LedState LedManagerTask::ledStateFromEvseState(uint8_t state)
{
#if RGB_LED
  switch(state)
  {
    case OPENEVSE_STATE_STARTING:
      return LedState_Unknown;

    case OPENEVSE_STATE_NOT_CONNECTED:
      return LedState_Ready;

    case OPENEVSE_STATE_CONNECTED:
      return LedState_Connected;

    case OPENEVSE_STATE_CHARGING:
      return LedState_Charging;

    case OPENEVSE_STATE_VENT_REQUIRED:
    case OPENEVSE_STATE_DIODE_CHECK_FAILED:
    case OPENEVSE_STATE_GFI_FAULT:
    case OPENEVSE_STATE_NO_EARTH_GROUND:
    case OPENEVSE_STATE_STUCK_RELAY:
    case OPENEVSE_STATE_GFI_SELF_TEST_FAILED:
    case OPENEVSE_STATE_OVER_TEMPERATURE:
    case OPENEVSE_STATE_OVER_CURRENT:
      return LedState_Error;

    case OPENEVSE_STATE_SLEEPING:
    case OPENEVSE_STATE_DISABLED:
      return LedState_Sleeping;
  }
#endif

  return LedState_Off;
}


int LedManagerTask::getPriority(LedState priorityState)
{
  switch (priorityState)
  {
    case LedState_Off:
      return 0;

    case LedState_WiFi_Client_Connected:
      return 10;

    case LedState_Unknown:
    case LedState_Ready:
    case LedState_Connected:
    case LedState_Charging:
    case LedState_Sleeping:
    case LedState_Warning:
      return 50;

    case LedState_WiFi_Access_Point_Waiting:
    case LedState_WiFi_Access_Point_Connected:
    case LedState_WiFi_Client_Connecting:
      return 100;

    case LedState_Error:
      return 1000;

    case LedState_Test_Red:
    case LedState_Test_Green:
    case LedState_Test_Blue:
      return 2000;
  }

  return 0;
}

void LedManagerTask::setNewState(bool wake)
{
  LedState newState = state < LedState_Off ? state : LedState_Off;
  int priority = getPriority(newState);

  LedState evseState = ledStateFromEvseState(this->evseState);
  int evsePriority = getPriority(evseState);
  if(evsePriority > priority) {
    newState = evseState;
    priority = evsePriority;
  }

  LedState wifiState = wifiClient ?
    (wifiConnected ? LedState_WiFi_Client_Connected : LedState_WiFi_Client_Connecting) :
    (wifiConnected ? LedState_WiFi_Access_Point_Connected : LedState_WiFi_Access_Point_Waiting);
  int wifiPriority = getPriority(wifiState);
  if(wifiPriority > priority) {
    newState = wifiState;
    priority = wifiPriority;
  }

  if(newState != state)
  {
    state = newState;
    if(wake) {
      MicroTask.wakeTask(this);
    }
  }
}

void LedManagerTask::clear()
{
  state = LedState_Off;
  MicroTask.wakeTask(this);
}

void LedManagerTask::test()
{
  state = LedState_Test_Red;
  MicroTask.wakeTask(this);
}

void LedManagerTask::setEvseState(uint8_t newState)
{
  DBUGVAR(newState);
  evseState = newState;
  setNewState();
}

void LedManagerTask::setWifiMode(bool client, bool connected)
{
  DBUGF("WiFi mode %d %d", client, connected);
  wifiClient = client;
  wifiConnected = connected;
  setNewState();
}

int LedManagerTask::getButtonPressed()
{
#if defined(WIFI_BUTTON_SHARE_LED)
  #ifdef ESP32
  int channel = analogWriteChannel(WIFI_BUTTON_SHARE_LED);
  if(-1 != channel) {
    ledcDetachPin(WIFI_BUTTON_SHARE_LED);
  }
  #endif

  digitalWrite(WIFI_BUTTON_SHARE_LED, HIGH);
  pinMode(WIFI_BUTTON_SHARE_LED, WIFI_BUTTON_PRESSED_PIN_MODE);
#endif

  // Pressing the boot button for 5 seconds will turn on AP mode, 10 seconds will factory reset
  int button = digitalRead(WIFI_BUTTON);

#if defined(WIFI_BUTTON_SHARE_LED)
  pinMode(WIFI_BUTTON, OUTPUT);

  #ifdef ESP32
  if(-1 != channel) {
    ledcAttachPin(WIFI_BUTTON_SHARE_LED, channel);
  }
  #endif

  if(0 == buttonShareState || 255 == buttonShareState) {
    digitalWrite(WIFI_BUTTON_SHARE_LED, buttonShareState ? HIGH : LOW);
  } else {
    analogWrite(WIFI_BUTTON_SHARE_LED, buttonShareState);
  }
#endif

  return button;
}

void LedManagerTask::setBrightness(uint8_t brightness)
{
  // Stored brightness value is different than what's passed.
  // This simplifies the actual scaling math later, allowing a fast
  // 8x8-bit multiply and taking the MSB. 'brightness' is a uint8_t,
  // adding 1 here may (intentionally) roll over...so 0 = max brightness
  // (color values are interpreted ltimingiterally; no scaling), 1 = min
  // brightness (off), 255 = just below max brightness.
  this->brightness = brightness - 1;

//#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH)
//  strip.setBrightness(LED_DEFAULT_BRIGHTNESS);
//#endif

  DBUGVAR(this->brightness);

  // Wake the task to refresh the state
  MicroTask.wakeTask(this);
}

LedManagerTask ledManager;
