#ifndef LED_MANAGER_TASK_H
#define LED_MANAGER_TASK_H

#include <Arduino.h>
#include <MicroTasks.h>

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
  LedState_Unknown,
  LedState_Ready,
  LedState_Connected,
  LedState_Charging,
  LedState_Sleeping,
  LedState_Warning,
  LedState_Error,
  LedState_WiFi_Access_Point_Waiting,
  LedState_WiFi_Access_Point_Connected,
  LedState_WiFi_Client_Connecting,
  LedState_WiFi_Client_Connected
};

class LedManagerTask : public MicroTasks::Task
{
  private:
    LedState state;
    uint8_t evseState;
    bool wifiClient;
    bool wifiConnected;

    bool flashState;

#if RGB_LED
    void setAllRGB(uint8_t red, uint8_t green, uint8_t blue);
#endif

#ifdef WIFI_LED
    void setWiFiLed(uint8_t state);
#endif

    LedState ledStateFromEvseState(uint8_t);
    void setNewState(bool wake = true);
    int getPriority(LedState state);

  public:
    LedManagerTask();

    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

    void setEvseState(uint8_t state);
    void setWifiMode(bool client, bool connected);

    void test();
    void clear();

    int getButtonPressed();
};

extern LedManagerTask ledManager;

#endif //  LED_MANAGER_TASK_H
