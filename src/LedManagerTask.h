#ifndef LED_MANAGER_TASK_H
#define LED_MANAGER_TASK_H

#include <Arduino.h>
#include <MicroTasks.h>

#include "evse_man.h"

#if defined(NEO_PIXEL_PIN) && defined(NEO_PIXEL_LENGTH)
#define RGB_LED 1
#elif defined(RED_LED) && defined(GREEN_LED) && defined(BLUE_LED)
#define RGB_LED 1
#else
#define RGB_LED 0
#endif

enum LedState
{
  LedState_Test_Red,
  LedState_Test_Green,
  LedState_Test_Blue,
  LedState_Off,
  LedState_Evse_State,
  LedState_WiFi_Access_Point_Waiting,
  LedState_WiFi_Access_Point_Connected,
  LedState_WiFi_Client_Connecting,
  LedState_WiFi_Client_Connected
};

class LedManagerTask : public MicroTasks::Task
{
  private:
    EvseManager *_evse;

    LedState state;

    bool wifiClient;
    bool wifiConnected;

    bool flashState;

    uint8_t brightness;

    MicroTasks::EventListener onStateChange;

#if RGB_LED
    void setAllRGB(uint8_t red, uint8_t green, uint8_t blue);
#endif

#ifdef WIFI_LED
    void setWiFiLed(uint8_t state);
#endif

    LedState ledStateFromEvseState(uint8_t);
    void setNewState(bool wake = true);
    int getPriority(LedState state);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    LedManagerTask();

    void begin(EvseManager &evse);

    void setWifiMode(bool client, bool connected);

    void test();
    void clear();

    int getButtonPressed();

    void setBrightness(uint8_t brightness);
};

extern LedManagerTask ledManager;

#endif //  LED_MANAGER_TASK_H
