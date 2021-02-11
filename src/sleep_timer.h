
#ifndef SLEEP_TIMER_H
#define SLEEP_TIMER_H
#include <ArduinoJson.h>

#define SLEEP_TIMER_NOT_CONNECTED_FLAG (1 << 0)
#define SLEEP_TIMER_CONNECTED_FLAG (1 << 1)
#define SLEEP_TIMER_DISCONNECTED_FLAG (1 << 2)


extern void sleep_timer_loop();

// -------------------------------------------------------------------
// Called when the state changes from sleeping to not connected
// -------------------------------------------------------------------
extern void on_wake_up();

// -------------------------------------------------------------------
// Called when the state changes to connected
// -------------------------------------------------------------------
extern void on_vehicle_connected();

// -------------------------------------------------------------------
// Called when the state changes from connected to disconnected
// -------------------------------------------------------------------
extern void on_vehicle_disconnected();

// -------------------------------------------------------------------
// Enable/disable display updates
// When enabled, the timer loop will display timer information
// on the lcd regularly.
// -------------------------------------------------------------------
extern void sleep_timer_display_updates(bool enabled);

#endif