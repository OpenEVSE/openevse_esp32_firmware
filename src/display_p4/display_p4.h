#ifndef DISPLAY_P4_H
#define DISPLAY_P4_H
#if defined(ENABLE_SCREEN_LVGL)

#include <MicroTasks.h>

// LVGL display + GT911 touch + backlight, pumped cooperatively by the MicroTask
// scheduler. Replaces the D1 free function + raw FreeRTOS task.
class DisplayP4Task : public MicroTasks::Task
{
public:
  DisplayP4Task();
  void begin();           // register with the MicroTask scheduler
  void wakeBacklight();   // call on user activity to re-light + reset the idle timer

protected:
  void setup();
  unsigned long loop(MicroTasks::WakeReason reason);

private:
  unsigned long _backlightDeadline;
  bool _backlightOn;
};

extern DisplayP4Task displayP4;

#endif // ENABLE_SCREEN_LVGL
#endif // DISPLAY_P4_H
