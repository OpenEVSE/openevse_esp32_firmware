// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#include <Arduino.h>
#include "emonesp.h"

// 2: Normal / Fast Charge (default):
// Charging at maximum rate irrespective of solar PV / grid_ie output

// 1: Eco :
// Either modulate charge rate based solar PV generation (if only solar PV feed is available)

// Or modulate charge rate based on on excess power (if grid feed (positive import / negative export) is available) i.e. power that would otherwise be exported to the grid is diverted to EVSE.
// Note: it's Assumed EVSE power is included in grid feed e.g. (charge rate = gen - use - EVSE).

// If EVSE is sleeping charging will not start until solar PV / excess power > min chanrge rate
// Once charging begins it will not pause even if solaer PV / excess power drops less then minimm charge rate. This avoids wear on the relay and the car



// Default to normal charging unless set. Divert mode always defaults back to 1 if unit is reset (divertmode not saved in EEPROM)
byte divertmode = 1;


// Update divert mode e.g. Normal / Eco
void divertmode_update(byte newmode){
  divertmode = newmode;
}

// Set charge rate depending on divert mode and solar / grid_ie
void divert_current_loop(){
  DEBUG.print("Divert mode: ");
  DEBUG.println(divertmode);
}
