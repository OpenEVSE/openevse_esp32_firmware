#ifndef HOME_BATTERY_H
#define HOME_BATTERY_H

#include <ArduinoJson.h>

// Display-only home/powerwall battery values pushed in via POST /status
// (keys "home_battery_soc" / "home_battery_power") or an MQTT topic. Each value
// is omitted from /status until it has been received at least once.
extern int  home_battery_soc;
extern bool home_battery_soc_valid;
extern int  home_battery_power;
extern bool home_battery_power_valid;

void home_battery_set_soc(int soc);
void home_battery_set_power(int power);

// Merge the valid display-only home-battery values into a /status document.
void home_battery_add_status_fields(JsonDocument &doc);

#endif // HOME_BATTERY_H
