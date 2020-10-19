// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#include <Arduino.h>
#include "emonesp.h"
#include "input.h"
#include "app_config.h"
#include "RapiSender.h"
#include "mqtt.h"
#include "event.h"
#include "openevse.h"
#include "divert.h"

#include <sys/time.h>

// 1: Normal / Fast Charge (default):
// Charging at maximum rate irrespective of solar PV / grid_ie output

// 2: Eco :
// Either modulate charge rate based solar PV generation (if only solar PV feed is available)

// Or modulate charge rate based on on excess power (if grid feed (positive import / negative export) is available) i.e. power that would otherwise be exported to the grid is diverted to EVSE.
// Note: it's Assumed EVSE power is included in grid feed e.g. (charge rate = gen - use - EVSE).

// It's better to never import current from the grid but because of charging current quantification (min value of 6A and change in steps of 1A),
// it may be better to import a fraction of the charge current and use the next charging level sooner, depending on electricity buying/selling prices.
// The marginal fraction of current that is required to come from PV is the divert_PV_ratio. The requiring a pure PV charge is obtained by
// setting divert_PV_ratio to 1.0. This is the best choice when the kWh selling price is the same as the kWh night buying price.  If instead the night tarif
// is the same as the day tarif, any available current is good to take and divert_PV_ratio optimal setting is close to 0.0. Beyond 1.0, the excess of
// divert_PV_ratio indicates the amount of power (in kW) that OpenEVSE will try to preserve. A value of 1.1 will start charging only when the
// PV power - 100 W (reserve) reaches the minimum charging power (reproducing the legacy behavior of OpenEVSE).

// If EVSE is sleeping, charging will start as soon as solar PV / excess power exceeds the divert_PV_ratio fraction of the minimum charging power.
// Once charging begins it will not pause before a minimum amount of time has passed and this even if solar PV / excess power drops less then minimum charge rate.
// This avoids wear on the relay and the car.

// Default to normal charging unless set. Divert mode always defaults back to 1 if unit is reset (divertmode not saved in EEPROM)
byte divertmode = DIVERT_MODE_NORMAL;     // default normal mode
int solar = 0;
int grid_ie = 0;
int min_charge_current = 6;      // TO DO: set to be min charge current as set on the OpenEVSE e.g. "$GC min-current max-current"
int max_charge_current = 32;     // TO DO: to be set to be max charge current as set on the OpenEVSE e.g. "$GC min-current max-current"
int charge_rate = 0;
int last_state = OPENEVSE_STATE_INVALID;
uint32_t lastUpdate = 0;


double available_current = 0;
double smoothed_available_current = 0;

time_t min_charge_end = 0;

bool divert_active = false;

extern RapiSender rapiSender;

// define as 'weak' so the simulator can override
time_t __attribute__((weak)) divertmode_get_time()
{
  struct timeval now;
  gettimeofday(&now, NULL);
  return now.tv_sec;
}

// Update divert mode e.g. Normal / Eco
// function called when divert mode is changed
void divertmode_update(byte newmode)
{
  DBUGF("Set divertmode: %d", newmode);
  if(divertmode != newmode)
  {
    divertmode = newmode;

    // restore max charge current if normal mode or zero if eco mode
    switch(divertmode)
    {
      case DIVERT_MODE_NORMAL:
        // Restore the max charge current
        rapiSender.sendCmdSync(String(F("$SC ")) + String(max_charge_current));
        DBUGF("Restore max I: %d", max_charge_current);
        break;

      case DIVERT_MODE_ECO:
        charge_rate = 0;
        available_current = 0;
        smoothed_available_current = 0;
        min_charge_end = 0;
        
        // Read the current charge current, assume this is the max set by the user
        if(0 == rapiSender.sendCmdSync(F("$GE"))) {
          max_charge_current = String(rapiSender.getToken(1)).toInt();
          DBUGF("Read max I: %d", max_charge_current);
        }
        if(OPENEVSE_STATE_SLEEPING != state)
        {
          if(0 == rapiSender.sendCmdSync(config_pause_uses_disabled() ? F("$FD") : F("$FS"))) 
          {
            DBUGLN(F("Divert activated, entered sleep mode"));
            divert_active = false;
          }
        }
        break;

      default:
        return;
    }

    StaticJsonDocument<128> event;
    event["divertmode"] = divertmode;
    event["divert_active"] = divert_active;
    event_send(event);
  }
}

void divert_current_loop()
{
  Profile_Start(divert_current_loop);

  Profile_End(divert_current_loop, 5);
} //end divert_current_loop

// Set charge rate depending on divert mode and solar / grid_ie
void divert_update_state()
{
  Profile_Start(divert_update_state);

  StaticJsonDocument<128> event;
  event["divert_update"] = 0;

  if(mqtt_grid_ie != "") {
    event["grid_ie"] = grid_ie;
  } else {
    event["solar"] = solar;
  }

  // If divert mode = Eco (2)
  if (divertmode == DIVERT_MODE_ECO)
  {
    int current_charge_rate = charge_rate;

    // Read the current charge rate
    if(0 == rapiSender.sendCmdSync(F("$GE"))) {
      current_charge_rate = String(rapiSender.getToken(1)).toInt();
      DBUGVAR(current_charge_rate);
    }

    // Calculate current
    if (mqtt_grid_ie != "")
    {
      // if grid feed is available and exporting increment the charge rate,
      // if importing drop the charge rate.
      // grid_ie is negative when exporting
      // If grid feeds is available and exporting (negative)

      DBUGVAR(voltage);
      double Igrid_ie = (double)grid_ie / voltage;
      DBUGVAR(Igrid_ie);

      // Subtract the current charge the EV is using from the Grid IE
      if(0 == rapiSender.sendCmdSync(F("$GG"))) {
        int milliAmps = String(rapiSender.getToken(1)).toInt();
        double amps = (double)milliAmps / 1000.0;
        DBUGVAR(amps);
        Igrid_ie -= amps;
        DBUGVAR(Igrid_ie);
      }

      if (Igrid_ie < 0)
      {
        // If excess power
        double reserve = (1000.0 * ((divert_PV_ratio > 1.0) ? (divert_PV_ratio - 1.0) : 0.0)) / voltage;
        DBUGVAR(reserve);
        available_current = (-Igrid_ie - reserve);
      }
      else
      {
        // no excess, so use the min charge
        available_current = 0;
      }
    }
    else if (mqtt_solar!="")
    {
      // if grid feed is not available: charge rate = solar generation
      DBUGVAR(voltage);
      available_current = (double)solar / voltage;
    }

    if(available_current < 0) {
      available_current = 0;
    }
    DBUGVAR(available_current);

    double scale = (available_current > smoothed_available_current ? divert_attack_smoothing_factor : divert_decay_smoothing_factor);
    smoothed_available_current = (available_current * scale) + (smoothed_available_current * (1 - scale));
    DBUGVAR(smoothed_available_current);

    charge_rate = (int)floor(available_current);
    // if the remaining current can be used with a sufficient ratio of PV current in it, use it
    if ((available_current - charge_rate) > min(1.0, divert_PV_ratio))
    {
      charge_rate += 1;
    }

    if(OPENEVSE_STATE_SLEEPING != state) {
      // If we are not sleeping, make sure we are the minimum current
      charge_rate = max(charge_rate, static_cast<int>(min_charge_current));
    }

    DBUGVAR(charge_rate);

    // the smoothed current suffices to ensure a sufficient ratio of PV power
    if (smoothed_available_current >= (min_charge_current * min(1.0, divert_PV_ratio)))
    {
      // Cap the charge rate at the configured maximum
      charge_rate = min(charge_rate, static_cast<int>(max_charge_current));

      // Change the charge rate if needed
      if(current_charge_rate != charge_rate)
      {
        // Set charge rate via RAPI
        bool chargeRateSet = false;
        // Try and set current with new API with volatile flag (don't save the current rate to EEPROM)
        if(0 == rapiSender.sendCmdSync(String(F("$SC ")) + String(charge_rate) + String(F(" V")))) {
          chargeRateSet = true;
        } else if(0 == rapiSender.sendCmdSync(String(F("$SC ")) + String(charge_rate))) {
          // Fallback to old API
          chargeRateSet = true;
        }

        if(true == chargeRateSet) 
        {
          DBUGF("Charge rate set to %d", charge_rate);
          pilot = charge_rate;
        }
      }

      // If charge rate > min current and EVSE is sleeping then start charging
      if (state == OPENEVSE_STATE_SLEEPING)
      {
        DBUGLN(F("Wake up EVSE"));
        bool chargeStarted = false;

        // Check if the timer is enabled, we need to do a bit of hackery if it is
        if(0 == rapiSender.sendCmdSync("$GD"))
        {
          if(rapiSender.getTokenCnt() >= 5 &&
             (0 != String(rapiSender.getToken(1)).toInt() ||
              0 != String(rapiSender.getToken(2)).toInt() ||
              0 != String(rapiSender.getToken(3)).toInt() ||
              0 != String(rapiSender.getToken(4)).toInt()))
          {
            // Timer is enabled so we need to emulate a button press to work around
            // an issue with $FE not working
            if(false == chargeStarted && 0 == rapiSender.sendCmdSync(F("$F1"))) {
              DBUGLN(F("Starting charge with button press"));
              chargeStarted = true;
            }
          }
        }

        if(false == chargeStarted && 0 == rapiSender.sendCmdSync(F("$FE"))) {
          DBUGLN(F("Starting charge"));
          chargeStarted = true;
        }

        if(chargeStarted) 
        {
          min_charge_end = divertmode_get_time() + divert_min_charge_time;
          event["divert_active"] = divert_active = true;
        }
      }
    }
    else
    {
      if(OPENEVSE_STATE_SLEEPING != state)
      {
        if(divert_active && divertmode_get_time() >= min_charge_end) 
        {
          if(0 == rapiSender.sendCmdSync(config_pause_uses_disabled() ? F("$FD") : F("$FS"))) 
          {
            DBUGLN(F("Charge Stopped"));
            event["divert_active"] = divert_active = false;

            if(0 == rapiSender.sendCmdSync(String(F("$SC ")) + String(max_charge_current))) {
              DBUGF("Restore max I: %d", max_charge_current);
            }
          }
        }
      }
    }

    event["charge_rate"] = charge_rate;
    event["voltage"] = voltage;
    event["available_current"] = available_current;
    event["smoothed_available_current"] = smoothed_available_current;
  } // end ecomode

  event_send(event);

  lastUpdate = millis();

  Profile_End(divert_update_state, 5);
} //end divert_update_state
