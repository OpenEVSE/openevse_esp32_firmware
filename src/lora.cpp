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
#include "lora.h"

#include "app_config.h"


#define LORA_HTOI(c) ((c<='9')?(c-'0'):((c<='F')?(c-'A'+10):((c<='f')?(c-'a'+10):(0))))
#define LORA_TWO_HTOI(h, l) ((LORA_HTOI(h) << 4) + LORA_HTOI(l))
#define LORA_HEX_TO_BYTE(a, h, n) { for (int i = 0; i < n; i++) (a)[i] = LORA_TWO_HTOI(h[2*i], h[2*i + 1]); }
#define LORA_DEVADDR(a) (uint32_t) ((uint32_t) (a)[3] | (uint32_t) (a)[2] << 8 | (uint32_t) (a)[1] << 16 | (uint32_t) (a)[0] << 24)

#define ANNOUNCE_INTERVAL 30 * 1000 // (In Milliseconds)


// TODO: Store these via WebUI? We're doing (simple) ABP activation for now
const char *devAddr = "00000000";
const char *nwkSKey = "00000000000000000000000000000000";
const char *appSKey = "00000000000000000000000000000000";


// LoRaWAN credentials to use
static uint8_t DEVADDR[4];
static uint8_t NWKSKEY[16];
static uint8_t APPSKEY[16];

// Next LoRaWAN announcement
unsigned long nextAnnounce;

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

/// Reset LoRa modem. Reload LoRaWAN keys
void lora_reset()
{
  LORA_HEX_TO_BYTE(DEVADDR, devAddr, 4);
  LORA_HEX_TO_BYTE(NWKSKEY, nwkSKey, 16);
  LORA_HEX_TO_BYTE(APPSKEY, appSKey, 16);

  LMIC_reset();
  LMIC_setSession (0x13, LORA_DEVADDR(DEVADDR), NWKSKEY, APPSKEY);
  LMIC_setAdrMode(0);
  LMIC_setClockError(MAX_CLOCK_ERROR * 10 / 100);
  LMIC_selectSubBand(1);
  LMIC_setLinkCheckMode(0);
  LMIC.dn2Dr = DR_SF7;
}


/// Initial setup of LoRa modem.
void lora_setup()
{
  Profile_Start(lora_setup);

  os_init();
  lora_reset();

  // Set us up for an immeadiate announcement
  nextAnnounce = millis();
 
  Profile_End(lora_setup, 1);
}


void lora_publish(uint8_t *dataPacket)
{
  if (millis() < nextAnnounce)
    return;

  Profile_Start(lora_loop);
  DBUGF("LoRa: Starting LoRaWAN broadcast...");
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    DBUGF("LoRa: Modem busy. Retry later");
    return;
  } 

  LMIC_setTxData2(1, dataPacket, sizeof(dataPacket), true);
  nextAnnounce = millis() + ANNOUNCE_INTERVAL;

  Profile_End(lora_loop, 1);
}

#else /* !ENABLE_LORA */

#include "emonesp.h"
#include "app_config.h"

void lora_setup() { /*NOP*/ }
void lora_reset() { /*NOP*/ }
void lora_publish(uint8_t *dataPacket) { /*NOP*/ }

#endif
