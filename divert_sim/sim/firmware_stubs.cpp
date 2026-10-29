// Link-time stand-ins for firmware services the simulator does not model.
//
// rfid.cpp reports card events on the character LCD; the simulator has no
// display, so the LcdTask the firmware modules call into is a no-op.

#include "lcd.h"

LcdTask lcd;

LcdTask::LcdTask() :
  MicroTasks::Task(),
  _evseStateEvent(this),
  _evseSettingsEvent(this)
{
}

void LcdTask::setup() {}
unsigned long LcdTask::loop(MicroTasks::WakeReason) { return MicroTask.Infinate; }
void LcdTask::display(const __FlashStringHelper *, int, int, int, uint32_t) {}
void LcdTask::display(String &, int, int, int, uint32_t) {}
void LcdTask::display(const char *, int, int, int, uint32_t) {}
