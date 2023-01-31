/*
 * Copyright (c) 2019-2020 Alexander von Gluck IV for OpenEVSE
 *
 * -------------------------------------------------------------------
 *
 * Additional Adaptation of OpenEVSE ESP Wifi
 * by Trystan Lea, Glyn Hudson, OpenEnergyMonitor
 * All adaptation GNU General Public License as below.
 *
 * -------------------------------------------------------------------
 *
 * This file is part of Open EVSE.
 * Open EVSE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * Open EVSE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with Open EVSE; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifdef ENABLE_LORA

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#include "emonesp.h"
#include "input.h"
#include "lora.h"

#include "app_config.h"


#define LORA_HTOI(c) ((c<='9')?(c-'0'):((c<='F')?(c-'A'+10):((c<='f')?(c-'a'+10):(0))))
#define LORA_TWO_HTOI(h, l) ((LORA_HTOI(h) << 4) + LORA_HTOI(l))
#define LORA_HEX_TO_BYTE(a, h, n) { for (int i = 0; i < n; i++) (a)[i] = LORA_TWO_HTOI(h[2*i], h[2*i + 1]); }
#define LORA_DEVADDR(a) (uint32_t) ((uint32_t) (a)[3] | (uint32_t) (a)[2] << 8 | (uint32_t) (a)[1] << 16 | (uint32_t) (a)[0] << 24)

// Announce every 15 minutes
#define ANNOUNCE_INTERVAL 900 * 1000 // (In Milliseconds)


// LoRa module pin mapping
const lmic_pinmap lmic_pins = {
  .nss = LORA_NSS,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = LORA_RST,
  .dio = {LORA_DIO0, LORA_DIO1, LORA_DIO2},
};

// Used for OTAA, not used (yet)
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

void
create_rapi_cayennelpp(EvseManager* _evse, CayenneLPP* lpp)
{
  if (_evse == NULL) {
    DBUGF("Corrupt EvseManager!")
    return;
  }
  if (lpp == NULL) {
    DBUGF("Corrupt CayenneLPP buffer!")
    return;
  }

  lpp->reset();
  lpp->addDigitalInput(0, _evse->getEvseState());
  lpp->addAnalogInput(1, _evse->getVoltage());
  lpp->addAnalogInput(2, _evse->getAmps());
  lpp->addAnalogInput(3, _evse->getChargeCurrent());
  lpp->addDigitalInput(4, _evse->getSessionElapsed() / 60);
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_MONITOR))
    lpp->addTemperature(5, _evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR) * TEMP_SCALE_FACTOR);
  if(evse.isTemperatureValid(EVSE_MONITOR_TEMP_MAX))
    lpp->addTemperature(6, _evse->getTemperature(EVSE_MONITOR_TEMP_MAX) * TEMP_SCALE_FACTOR);
}

/// Reset LoRa modem. Reload LoRaWAN keys
void onEvent(ev_t ev) {
  switch (ev) {
    case EV_TXCOMPLETE:
      DBUGF("LoRa: TX Complete.");
      // LoRaWAN transmission complete
      if (LMIC.txrxFlags & TXRX_ACK) {
        // Received ack
        DBUGF("LoRa: TX ack.");
      }
      break;
    case EV_TXSTART:
      DBUGF("LoRa: TX Begin.");
      break;
    default:
      // Ignore anything else for now
      break;
  }
}

LoraTask::LoraTask()
  :
  MicroTasks::Task()
{
}

void
LoraTask::begin(EvseManager &evse)
{
    _evse = &evse;
    MicroTask.startTask(this);
}

/// Initial setup of LoRa modem.
void
LoraTask::setup()
{
  Profile_Start(LoraTask::setup);

  os_init();
  modem_reset();

  Profile_End(LoraTask::setup, 1);
}

/// Reset LoRa modem. Reload LoRaWAN keys
void
LoraTask::modem_reset()
{
  Profile_Start(LoraTask::modem_reset);
  // LoRaWAN credentials to use
  uint8_t DEVADDR[4];
  uint8_t NWKSKEY[16];
  uint8_t APPSKEY[16];

  LORA_HEX_TO_BYTE(DEVADDR, lora_deveui.c_str(), 4);
  LORA_HEX_TO_BYTE(NWKSKEY, lora_appeui.c_str(), 16);
  LORA_HEX_TO_BYTE(APPSKEY, lora_appkey.c_str(), 16);

  LMIC_reset();
  LMIC_setSession (0x13, LORA_DEVADDR(DEVADDR), NWKSKEY, APPSKEY);
  LMIC_setAdrMode(0);
  LMIC_setClockError(MAX_CLOCK_ERROR * 10 / 100);
  LMIC_selectSubBand(1);
  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr = DR_SF7;
  Profile_End(LoraTask::modem_reset, 1);
}

/// Announce our status to LoraWAN if it's time
void
LoraTask::publish(CayenneLPP* lpp)
{
  Profile_Start(LoraTask::publish);
  DBUGF("LoRa: Starting LoRaWAN broadcast...");
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    DBUGF("LoRa: Modem busy. Retry later");
    return;
  }
  LMIC_setTxData2(1, lpp->getBuffer(), lpp->getSize(), false);
  Profile_End(LoraTask::publish, 1);
}

unsigned long
LoraTask::loop(MicroTasks::WakeReason reason)
{
  if (!config_lora_enabled())
    return MicroTask.Infinate;

  CayenneLPP lpp(24);
  create_rapi_cayennelpp(_evse, &lpp);
  lora.publish(&lpp);
  return ANNOUNCE_INTERVAL;
}

LoraTask lora;

#endif /* ENABLE_LORA */
