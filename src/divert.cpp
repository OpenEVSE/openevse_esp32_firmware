// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#include <Arduino.h>
#include "emonesp.h"

// 2: Normal / Fast Charge (default):
// Charging at maximum rate irrespective of solar PV output

// 1: Eco :
// Charging level is moderated to match available excess solar PV power. e.g. Solar PV gen - onsite consumption
// Charging is pasued if excess power (solar PV - consumption) drop below 6A (1.4kW)

// Default to normal charging unless set. Divert mode always defaults back to 1 if unit is reset (divertmode not saved in EEPROM)
byte divertmode = 1;

void divertmode_update(){
DEBUG.print("Divert mode: ");
DEBUG.println(divertmode);
}


void divertmode_update(byte newmode){
  divertmode = newmode;
}
