// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#include <Arduino.h>
#include "emonesp.h"
#include "input.h"
#include "config.h"

// 1: Normal / Fast Charge (default):
// Charging at maximum rate irrespective of solar PV / grid_ie output

// 2: Eco :
// Either modulate charge rate based solar PV generation (if only solar PV feed is available)

// Or modulate charge rate based on on excess power (if grid feed (positive import / negative export) is available) i.e. power that would otherwise be exported to the grid is diverted to EVSE.
// Note: it's Assumed EVSE power is included in grid feed e.g. (charge rate = gen - use - EVSE).

// If EVSE is sleeping charging will not start until solar PV / excess power > min chanrge rate
// Once charging begins it will not pause even if solaer PV / excess power drops less then minimm charge rate. This avoids wear on the relay and the car



// Default to normal charging unless set. Divert mode always defaults back to 1 if unit is reset (divertmode not saved in EEPROM)
byte divertmode = 1;
int solar = 0;
int grid_ie = 0;
byte min_charge_current = 6;
byte max_charge_current = 32;
int charge_rate = 0;

// Update divert mode e.g. Normal / Eco
// function called when divert mode is changed
void divertmode_update(byte newmode){
  DEBUG.println("Setting divertmode: " + newmode);
  divertmode = newmode;

  // restore max charge current if normal mode or zero if eco mode
  if (divertmode == 1) charge_rate = max_charge_current;
  if (divertmode == 2) charge_rate = 0;
}

// Set charge rate depending on divert mode and solar / grid_ie
void divert_current_loop(){
  Profile_Start(mqtt_loop);

  // If divert mode = Eco (2)
  if (divertmode == 2){

    int Isolar = 0;
    int Igrid_ie = 0;


    // L1: voltage is 110V
    if (service == 1){
      // Calculate current
      if (mqtt_solar!="") Isolar = solar / 110;
      if (mqtt_grid_ie!="") Igrid_ie = grid_ie / 110;

      // if grid feed is available and exporting: charge rate = export - EVSE current
      // grid_ie is negative when exporting
      // If grid feeds is available and exporting (negative)
      if ( (mqtt_grid_ie!="") || (Igrid_ie < 0) ) {
        // If excess power
        if ( (Igrid_ie + current_l1) < 0){
          charge_rate = (Igrid_ie + current_l1)*-1;
        }
      }
    } //end L1 service


    // L2: voltage is 240V
    if (service == 2) {
      // Calculate current
      if (mqtt_solar!="") Isolar = solar / 240;
      if (mqtt_grid_ie!="") Igrid_ie = grid_ie / 240;

      // if grid feed is available and exporting: charge rate = export - EVSE current
      // grid_ie is negative when exporting
      // If grid feeds is available and exporting (negative)
      if ( (mqtt_grid_ie!="") || (Igrid_ie < 0) ) {
        // If excess power
        if ( (Igrid_ie + current_l2) < 0){
          charge_rate = (Igrid_ie + current_l2)*-1;
        }
      }
    } //end L2 service

    // if grid feed is not available: charge rate = solar generation
    if ((mqtt_solar!="") && (mqtt_grid_ie="")) charge_rate = Isolar;

    // Set charge rate via RAPI
    Serial.print("$SC");
    Serial.println(charge_rate);
    delay(60);
    DEBUG.print("Set charge rate: "); DEBUG.println(charge_rate);

    // If charge rate > min current and EVSE is sleeping then start charging
    if ( (charge_rate > min_charge_current) && ((state == 254) || (state == 596)) ){
      DEBUG.print("Wake up EVSE");
      Serial.print("$FE");
    }
  } // end ecomode

  Profile_End(mqtt_loop, 5);
} //end divert_current_loop
