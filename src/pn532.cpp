/*
 * Author: Ammar Panjwani
 * Wiegand-only version (I2C/PN532 removed). Preserves storage & onCardDetected() behavior.
 */

#if defined(ENABLE_PN532)

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_NFCREADER)
#undef ENABLE_DEBUG
#endif

#include "pn532.h"
#include "app_config.h"
#include "debug.h"
#include "lcd.h"

#ifndef WIEGAND_D0_PIN
#define WIEGAND_D0_PIN 35 
#endif
#ifndef WIEGAND_D1_PIN
#define WIEGAND_D1_PIN 36
#endif

#define WIEGAND_MAX_BITS        100
#define WIEGAND_GAP_USEC        3000     // gap to consider a frame complete
#define REARM_AFTER_MS          1500     // allow next detection after this pause

#ifndef SCAN_DELAY
#define SCAN_DELAY              1000
#endif
#ifndef ACK_DELAY
#define ACK_DELAY               30
#endif
#ifndef MAXIMUM_UNRESPONSIVE_TIME
#define MAXIMUM_UNRESPONSIVE_TIME 60000UL
#endif
#ifndef AUTO_REFRESH_CONNECTION
#define AUTO_REFRESH_CONNECTION 30
#endif

// ----------- Wiegand capture state -----------
static volatile uint8_t  wg_bits[WIEGAND_MAX_BITS];
static volatile uint8_t  wg_bitCount = 0;
static volatile uint32_t wg_lastPulseUs = 0;

// using a mux to prevent anything from breaking while isrs run
static portMUX_TYPE wgMux = portMUX_INITIALIZER_UNLOCKED;

// D0 falling => '0'
static void IRAM_ATTR wiegand_isr_d0() {
  portENTER_CRITICAL_ISR(&wgMux);
  if (wg_bitCount < WIEGAND_MAX_BITS) {
    wg_bits[wg_bitCount++] = 0;
    wg_lastPulseUs = micros();
  }
  portEXIT_CRITICAL_ISR(&wgMux);
}

// D1 falling => '1'
static void IRAM_ATTR wiegand_isr_d1() {
  portENTER_CRITICAL_ISR(&wgMux);
  if (wg_bitCount < WIEGAND_MAX_BITS) {
    wg_bits[wg_bitCount++] = 1;
    wg_lastPulseUs = micros();
  }
  portEXIT_CRITICAL_ISR(&wgMux);
}

// ----------------- PN532 class -----------------
PN532::PN532() : MicroTasks::Task() {}

void PN532::begin() {
  // Set up Wiegand pins + ISRs
  pinMode(WIEGAND_D0_PIN, INPUT);
  pinMode(WIEGAND_D1_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(WIEGAND_D0_PIN), wiegand_isr_d0, FALLING);
  attachInterrupt(digitalPinToInterrupt(WIEGAND_D1_PIN), wiegand_isr_d1, FALLING);

  status       = DeviceStatus::NOT_ACTIVE;
  lastResponse = millis();
  pollCount    = 0;
  hasContact   = false;
  listen       = false; 
  MicroTask.startTask(this);
}


unsigned long PN532::loop(MicroTasks::WakeReason reason) {
  (void)reason;

  uint8_t  localBits[WIEGAND_MAX_BITS];
  uint8_t  count = 0;
  uint32_t nowUs = micros();

  portENTER_CRITICAL(&wgMux);
  if (wg_bitCount > 0 && (uint32_t)(nowUs - wg_lastPulseUs) > WIEGAND_GAP_USEC) {
    count = wg_bitCount;
    if (count > WIEGAND_MAX_BITS) count = WIEGAND_MAX_BITS;
    for (uint8_t i = 0; i < count; i++) localBits[i] = wg_bits[i];
    wg_bitCount = 0; 
  }
  portEXIT_CRITICAL(&wgMux);

  if (count > 0) {
    uint8_t bytes[(WIEGAND_MAX_BITS + 7) / 8] = {0};
    for (uint8_t i = 0; i < count; i++) {
      uint8_t byteIdx = i / 8;
      uint8_t bitIdx  = 7 - (i % 8);
      if (localBits[i]) bytes[byteIdx] |= (1 << bitIdx);
    }
    uint8_t byteCount = (count + 7) / 8;

    // Hex Conversion
    String uid;
    uid.reserve(byteCount * 2);
    static const char* hex = "0123456789ABCDEF";
    for (uint8_t i = 0; i < byteCount; i++) {
      uint8_t v = bytes[i];
      uid += hex[(v >> 4) & 0xF];
      uid += hex[v & 0xF];
    }

    status       = DeviceStatus::ACTIVE;
    lastResponse = millis();
    if (!hasContact) {
      onCardDetected(uid);  
      hasContact = true;
    }

    return REARM_AFTER_MS;
  }

  
  if (hasContact && (millis() - lastResponse) > REARM_AFTER_MS) {
    DBUGLN(F("[rfid] connection to Reader lost"));
    hasContact = false;
  }

  return ACK_DELAY; // small periodic tick
}

// ---------------- Useless now, Kept to prevet issues ----------------
void PN532::initialize() {

}

void PN532::poll() {
  // No PN532 poll in Wiegand-only mode.
}

void PN532::read() {
  // No PN532 read in Wiegand-only mode.
}

bool PN532::readerFailure() {
  return config_rfid_enabled() && status == DeviceStatus::FAILED;
}

// Global instance (as before)
PN532 pn532;

#endif // ENABLE_PN532
