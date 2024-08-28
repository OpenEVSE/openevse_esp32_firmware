#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LED)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <openevse.h>

#ifdef ESP32
//#include <analogWrite.h>
#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)
#ifndef RED_LEDC_CHANNEL
#define RED_LEDC_CHANNEL 1
#endif
#ifndef GREEN_LEDC_CHANNEL
#define GREEN_LEDC_CHANNEL 2
#endif
#ifndef BLUE_LEDC_CHANNEL
#define BLUE_LEDC_CHANNEL 3
#endif
#ifndef LEDC_FREQUENCY
#define LEDC_FREQUENCY 5000
#endif
#ifndef LEDC_RESOLUTION
#define LEDC_RESOLUTION 8
#endif
#endif
#endif

#include "debug.h"
#include "emonesp.h"
#include "LedManagerTask.h"

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && !defined(ENABLE_WS2812FX)
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEO_PIXEL_LENGTH, NEO_PIXEL_PIN, NEO_GRB + NEO_KHZ800);
#elif defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && defined(ENABLE_WS2812FX)
#include <WS2812FX.h>
WS2812FX ws2812fx = WS2812FX(NEO_PIXEL_LENGTH, NEO_PIXEL_PIN, NEO_GRB + NEO_KHZ800);

class LedAnimatorTask : public MicroTasks::Task
{
  public:
    void setup() {
    }
    unsigned long loop(MicroTasks::WakeReason reason) {
      ws2812fx.service();
      return 10;
    }
} animator;
#endif

#define FADE_STEP         16
#define FADE_DELAY        50

#define CONNECTING_FLASH_TIME 450
#define CONNECTED_FLASH_TIME  250

#if defined(ENABLE_WS2812FX)
// Speed for FX Bar Effects
#define DEFAULT_FX_SPEED 1000
#define CONNECTING_FX_SPEED 2000
#define CONNECTED_FX_SPEED  1000
#endif

#define TEST_LED_TIME     500

#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)

// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix
const uint8_t gamma8[] = {
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
#define WIFI_BUTTON_SHARE_LEDC_CHANNEL RED_LEDC_CHANNEL
#elif defined(GREEN_LED) && GREEN_LED == WIFI_BUTTON
#define WIFI_BUTTON_SHARE_LED GREEN_LED
#define WIFI_BUTTON_SHARE_LEDC_CHANNEL GREEN_LEDC_CHANNEL
#elif defined(BLUE_LED) && BLUE_LED == WIFI_BUTTON
#define WIFI_BUTTON_SHARE_LED BLUE_LED
#define WIFI_BUTTON_SHARE_LLEDC_CHANNELBLUE_LEDC_CHANNEL
#endif

#endif

#ifdef WIFI_BUTTON_SHARE_LED
uint8_t buttonShareState = 0;
#endif

#if RGB_LED

#define rgb(r,g,b) (r<<16|g<<8|b)

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && defined(ENABLE_WS2812FX)

static uint32_t status_colour_map(u_int8_t lcdcol)
{
  u_int32_t color;
  switch (lcdcol)
  {
  case OPENEVSE_LCD_OFF:
    color = 0x000000; // BLACK
    break;
  case OPENEVSE_LCD_RED:
    color = 0xFF0000;  // RED
    break;
  case OPENEVSE_LCD_GREEN:
    color = 0x00FF00; // GREEN
    break;
  case OPENEVSE_LCD_YELLOW:
    color = 0xFFFF00; // YELLOW
    break;
  case OPENEVSE_LCD_BLUE:
    color = 0x0000FF; // BLUE
    break;
  case OPENEVSE_LCD_VIOLET:
    color = 0xFF00FF; // VIOLET
    break;
  case OPENEVSE_LCD_TEAL:
    color = 0x00FFFF; // TEAL
    break;
  case OPENEVSE_LCD_WHITE:
    color = 0xFFFFFF; // WHITE
    break;
  }
  return color; // WHITE
}
#else
static uint32_t status_colour_map[] =
{
  rgb(0, 0, 0),       // OPENEVSE_LCD_OFF
  rgb(255, 0, 0),     // OPENEVSE_LCD_RED
  rgb(0, 255, 0),     // OPENEVSE_LCD_GREEN
  rgb(255, 255, 0),   // OPENEVSE_LCD_YELLOW
  rgb(0, 0, 255),     // OPENEVSE_LCD_BLUE
  rgb(255, 0, 255),   // OPENEVSE_LCD_VIOLET
  rgb(0, 255, 255),   // OPENEVSE_LCD_TEAL
  rgb(255, 255, 255), // OPENEVSE_LCD_WHITE
};
#endif
#endif

LedManagerTask::LedManagerTask() :
  MicroTasks::Task(),
  _evse(nullptr),
  state(LedState_Test_Red),
  wifiClient(false),
  wifiConnected(false),
  flashState(false),
  brightness(LED_DEFAULT_BRIGHTNESS),
  onStateChange(this)
{
}

void LedManagerTask::begin(EvseManager &evse)
{
  _evse = &evse;
  _evse->onStateChange(&onStateChange);
  MicroTask.startTask(this);
}

void LedManagerTask::setup()
{
#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && !defined(ENABLE_WS2812FX)
  DBUGF("Initialising NeoPixels");
  strip.begin();
  //strip.setBrightness(brightness);
  setAllRGB(0, 0, 0);
#elif defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && defined(ENABLE_WS2812FX)
  DEBUG.printf("Initialising NeoPixels WS2812FX MODE...\n");
  ws2812fx.init();
  ws2812fx.setBrightness(brightness);
  ws2812fx.setSpeed(DEFAULT_FX_SPEED);
  ws2812fx.setColor(BLACK);
  ws2812fx.setMode(FX_MODE_STATIC);
  //ws2812fx.setBrightness(this->brightness);
  DBUGF("Brightness: %d ", this->brightness);
  DBUGF("Brightness: %d ", brightness);

  ws2812fx.start();
  MicroTask.startTask(&animator);
#endif

#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)
  DBUGF("Initialising RGB LEDs, %d, %d, %d", RED_LED, GREEN_LED, BLUE_LED);
  // configure LED PWM functionalitites
  ledcSetup(RED_LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(RED_LED, RED_LEDC_CHANNEL);
  ledcSetup(GREEN_LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(GREEN_LED, GREEN_LEDC_CHANNEL);
  ledcSetup(BLUE_LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(BLUE_LED, BLUE_LEDC_CHANNEL);
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
         LedState_Evse_State == state ? "LedState_Evse_State" :
         LedState_WiFi_Access_Point_Waiting == state ? "LedState_WiFi_Access_Point_Waiting" :
         LedState_WiFi_Access_Point_Connected == state ? "LedState_WiFi_Access_Point_Connected" :
         LedState_WiFi_Client_Connected == state ? "LedState_WiFi_Client_Connected" :
         "UNKNOWN");

  if(onStateChange.IsTriggered()) {
    setNewState(false);
  }

#if RGB_LED
#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && defined(ENABLE_WS2812FX)
  switch(state)
  {
    case LedState_Off:
      //setAllRGB(0, 0, 0);
      ws2812fx.setColor(BLACK);
      return MicroTask.Infinate;

    case LedState_Test_Red:
      //setAllRGB(255, 0, 0);
      ws2812fx.setColor(RED);
      state = LedState_Test_Green;
      return TEST_LED_TIME;

    case LedState_Test_Green:
      //setAllRGB(0, 255, 0);
      ws2812fx.setColor(GREEN);
      state = LedState_Test_Blue;
      return TEST_LED_TIME;

    case LedState_Test_Blue:
      //setAllRGB(0, 0, 255);
      ws2812fx.setColor(BLUE);
      state = LedState_Off;
      setNewState(false);
      return TEST_LED_TIME;


    case LedState_Evse_State:
    case LedState_WiFi_Access_Point_Waiting:
    case LedState_WiFi_Access_Point_Connected:
    case LedState_WiFi_Client_Connecting:
    case LedState_WiFi_Client_Connected:
    {
      uint8_t lcdCol = _evse->getStateColour();
      DBUGVAR(lcdCol);
      uint32_t col = status_colour_map(lcdCol);
      DBUGVAR(col, HEX);
      //DBUGF("Color: %x\n", col);
      bool isCharging, isError;
      u_int16_t speed;
      speed = 2000 - ((_evse->getChargeCurrent()/_evse->getMaxHardwareCurrent())*1000);
      DBUGF("Speed: %d ",speed);
      DBUGF("Amps: %d ", _evse->getAmps());
      DBUGF("ChargeCurrent: %d ", _evse->getChargeCurrent());
      DBUGF("MaxHWCurrent: %d ", _evse->getMaxHardwareCurrent());
      if (this->brightness == 0){
        ws2812fx.setBrightness(255);
      }
      else {
        ws2812fx.setBrightness(this->brightness-1);
      }
      switch(state)
      {
        case LedState_Evse_State:
          isCharging = _evse->isCharging();
          isError = _evse->isError();
          if(isCharging){
            setAllRGB(col, FX_MODE_COLOR_WIPE, speed);
          } else if(isError){
            setAllRGB(col, FX_MODE_FADE, DEFAULT_FX_SPEED);
          } else {
            setAllRGB(col, FX_MODE_STATIC, DEFAULT_FX_SPEED);
          }    
          //DBUGF("MODE:  LedState_Evse_State\n");
          return MicroTask.Infinate;

        case LedState_WiFi_Access_Point_Waiting:
          setEvseAndWifiRGB(col, FX_MODE_BLINK, CONNECTING_FX_SPEED);
          //DBUGF("MODE: LedState_WiFi_Access_Point_Waiting\n");
          return CONNECTING_FLASH_TIME;

        case LedState_WiFi_Access_Point_Connected:
          setEvseAndWifiRGB(col, FX_MODE_FADE, CONNECTED_FX_SPEED);
          flashState = !flashState;
          //DBUGF("MODE: LedState_WiFi_Access_Point_Connected\n");
          return CONNECTED_FLASH_TIME;

        case LedState_WiFi_Client_Connecting:
          setEvseAndWifiRGB(col, FX_MODE_FADE, CONNECTING_FX_SPEED);
          flashState = !flashState;
          //DBUGF("MODE: LedState_WiFi_Client_Connecting\n");
          return CONNECTING_FLASH_TIME;

        case LedState_WiFi_Client_Connected:
          setEvseAndWifiRGB(col, FX_MODE_FADE, CONNECTED_FX_SPEED);
          //DBUGF("MODOE: LedState_WiFi_Client_Connected\n");
          return MicroTask.Infinate;

        default:
          break;
      }
    }

  }
#else
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

#ifdef WIFI_PIXEL_NUMBER
    case LedState_Evse_State:
    case LedState_WiFi_Access_Point_Waiting:
    case LedState_WiFi_Access_Point_Connected:
    case LedState_WiFi_Client_Connecting:
    case LedState_WiFi_Client_Connected:
    {
      uint8_t lcdCol = _evse->getStateColour();
      DBUGVAR(lcdCol);
      uint32_t col = status_colour_map[lcdCol];
      DBUGVAR(col, HEX);
      uint8_t evseR = (col >> 16) & 0xff;
      uint8_t evseG = (col >> 8) & 0xff;
      uint8_t evseB = col & 0xff;

      switch(state)
      {
        case LedState_Evse_State:
          setAllRGB(evseR, evseG, evseB);
          return MicroTask.Infinate;

        case LedState_WiFi_Access_Point_Waiting:
          setEvseAndWifiRGB(evseR, evseG, evseB, flashState ? 255 : 0, flashState ? 255 : 0, 0);
          flashState = !flashState;
          return CONNECTING_FLASH_TIME;

        case LedState_WiFi_Access_Point_Connected:
          setEvseAndWifiRGB(evseR, evseG, evseB, flashState ? 255 : 0, 0, flashState ? 255 : 0);
          flashState = !flashState;
          return CONNECTED_FLASH_TIME;

        case LedState_WiFi_Client_Connecting:
          setEvseAndWifiRGB(evseR, evseG, evseB, 0, flashState ? 255 : 0, flashState ? 255 : 0);
          flashState = !flashState;
          return CONNECTING_FLASH_TIME;

        case LedState_WiFi_Client_Connected:
          setEvseAndWifiRGB(evseR, evseG, evseB, 0, 255, 0);
          return MicroTask.Infinate;

        default:
          break;
      }
    }
#else
    case LedState_Evse_State:
    {
      uint8_t lcdCol = _evse->getStateColour();
      DBUGVAR(lcdCol);
      uint32_t col = status_colour_map[lcdCol];
      DBUGVAR(col, HEX);
      uint8_t r = (col >> 16) & 0xff;
      uint8_t g = (col >> 8) & 0xff;
      uint8_t b = col & 0xff;
      setAllRGB(r, g, b);
    } return MicroTask.Infinate;

    case LedState_WiFi_Access_Point_Waiting:
      setAllRGB(flashState ? 255 : 0, flashState ? 255 : 0, 0);
      flashState = !flashState;
      return CONNECTING_FLASH_TIME;

    case LedState_WiFi_Access_Point_Connected:
      setAllRGB(flashState ? 255 : 0, 0, flashState ? 255 : 0);
      flashState = !flashState;
      return CONNECTED_FLASH_TIME;

    case LedState_WiFi_Client_Connecting:
      setAllRGB(0, flashState ? 255 : 0, flashState ? 255 : 0);
      flashState = !flashState;
      return CONNECTING_FLASH_TIME;

    case LedState_WiFi_Client_Connected:
      setAllRGB(0, 255, 0);
      return MicroTask.Infinate;
#endif
  }
#endif
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
#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && defined(ENABLE_WS2812FX)
void LedManagerTask::setAllRGB(uint32_t color, u_int8_t mode, uint16_t speed)
{
  setEvseAndWifiRGB(color, mode, speed);
}


void LedManagerTask::setEvseAndWifiRGB(uint32_t evseColor, u_int8_t mode, u_int16_t speed)
{
  DBUG("EVSE LED COLOR:");
  DBUG(evseColor);
  if(evseColor != ws2812fx.getColor()){
    ws2812fx.setColor(evseColor);
  }

  if(speed != ws2812fx.getSpeed()){
    ws2812fx.setSpeed(speed);
  }

  if (ws2812fx.getMode() != mode){
    ws2812fx.setMode(mode);
  }

}
#else
void LedManagerTask::setAllRGB(uint8_t red, uint8_t green, uint8_t blue)
{
  setEvseAndWifiRGB(red, green, blue, red, green, blue);
}
#endif
#endif

#if WIFI_PIXEL_NUMBER && !defined(ENABLE_WS2812FX)
void LedManagerTask::setEvseAndWifiRGB(uint8_t evseRed, uint8_t evseGreen, uint8_t evseBlue, uint8_t wifiRed, uint8_t wifiGreen, uint8_t wifiBlue)
{
  DBUG("EVSE LED R:");
  DBUG(evseRed);
  DBUG(" G:");
  DBUG(evseGreen);
  DBUG(" B:");
  DBUGLN(evseBlue);

  DBUG("WiFi LED R:");
  DBUG(wifiRed);
  DBUG(" G:");
  DBUG(wifiGreen);
  DBUG(" B:");
  DBUGLN(wifiBlue);

  if(brightness) { // See notes in setBrightness()
    evseRed = (evseRed * brightness) >> 8;
    evseGreen = (evseGreen * brightness) >> 8;
    evseBlue = (evseBlue * brightness) >> 8;
    wifiRed = (wifiRed * brightness) >> 8;
    wifiGreen = (wifiGreen * brightness) >> 8;
    wifiBlue = (wifiBlue * brightness) >> 8;
  }

  DBUG("EVSE LED R:");
  DBUG(evseRed);
  DBUG(" G:");
  DBUG(evseGreen);
  DBUG(" B:");
  DBUGLN(evseBlue);

  DBUG("WiFi LED R:");
  DBUG(wifiRed);
  DBUG(" G:");
  DBUG(wifiGreen);
  DBUG(" B:");
  DBUGLN(wifiBlue);

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && !defined(ENABLE_WS2812FX)
  uint32_t col = strip.gamma32(strip.Color(evseRed, evseGreen, evseBlue));
  DBUGVAR(col, HEX);
  strip.fill(col);
  strip.setPixelColor(WIFI_PIXEL_NUMBER, wifiRed, wifiGreen, wifiBlue);
  strip.show();
#endif

#if defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)

  DBUGVAR(gamma8[wifiRed]);
  DBUGVAR(gamma8[wifiGreen]);
  DBUGVAR(gamma8[wifiBlue]);

  ledcWrite(RED_LEDC_CHANNEL, gamma8[wifiRed]);
  ledcWrite(GREEN_LEDC_CHANNEL, gamma8[wifiGreen]);
  ledcWrite(BLUE_LEDC_CHANNEL, gamma8[wifiBlue]);

#ifdef WIFI_BUTTON_SHARE_LED
  #if RED_LED == WIFI_BUTTON_SHARE_LED
    buttonShareState = gamma8[wifiRed];
  #elif GREEN_LED == WIFI_BUTTON_SHARE_LED
    buttonShareState = gamma8[wifiGreen];
  #elif BLUE_LED == WIFI_BUTTON_SHARE_LED
    buttonShareState = gamma8[wifiBlue];
  #endif
  DBUGVAR(buttonShareState);
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

int LedManagerTask::getPriority(LedState priorityState)
{
  switch (priorityState)
  {
    case LedState_Off:
      return 0;

    case LedState_WiFi_Client_Connected:
      return 10;

    case LedState_Evse_State:
      return _evse->isError() ? 1000 : 50;

    case LedState_WiFi_Access_Point_Waiting:
    case LedState_WiFi_Access_Point_Connected:
    case LedState_WiFi_Client_Connecting:
      return 100;

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

  int evsePriority = getPriority(LedState_Evse_State);
  DBUGVAR(evsePriority);
  if(evsePriority > priority) {
    newState = LedState_Evse_State;
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
  #ifdef RGB_LEDC_CHANNEL
  ledcDetachPin(WIFI_BUTTON_SHARE_LED);
  #else
  digitalWrite(WIFI_BUTTON_SHARE_LED, HIGH);
  #endif

  pinMode(WIFI_BUTTON_SHARE_LED, WIFI_BUTTON_PRESSED_PIN_MODE);
#endif

  // Pressing the boot button for 5 seconds will turn on AP mode, 10 seconds will factory reset
  int button = digitalRead(WIFI_BUTTON);

#if defined(WIFI_BUTTON_SHARE_LED)
  #ifdef WIFI_BUTTON_SHARE_LEDC_CHANNEL
  ledcAttachPin(WIFI_BUTTON_SHARE_LED, WIFI_BUTTON_SHARE_LEDC_CHANNEL);
  ledcWrite(WIFI_BUTTON_SHARE_LED, buttonShareState);
  #else
  pinMode(WIFI_BUTTON_SHARE_LED, OUTPUT);
  digitalWrite(WIFI_BUTTON_SHARE_LED, buttonShareState ? HIGH : LOW);
  #endif
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
  this->brightness = brightness + 1;

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH) && defined(ENABLE_WS2812FX)
// This controls changes on the limits of the web interface slidebar.
// Otherwise it gets out of sync
  if (this->brightness == 0){
    ws2812fx.setBrightness(255);
  }
  else {
    ws2812fx.setBrightness(this->brightness-1);
  }

#endif

  DBUGVAR(this->brightness);

  // Wake the task to refresh the state
  MicroTask.wakeTask(this);
}


LedManagerTask ledManager;