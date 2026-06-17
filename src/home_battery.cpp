#include "home_battery.h"

int  home_battery_soc = 0;
bool home_battery_soc_valid = false;
int  home_battery_power = 0;
bool home_battery_power_valid = false;

void home_battery_set_soc(int soc)
{
  home_battery_soc = soc;
  home_battery_soc_valid = true;
}

void home_battery_set_power(int power)
{
  home_battery_power = power;
  home_battery_power_valid = true;
}

void home_battery_add_status_fields(JsonDocument &doc)
{
  if (home_battery_soc_valid)   doc["home_battery_soc"]   = home_battery_soc;
  if (home_battery_power_valid) doc["home_battery_power"] = home_battery_power;
}
