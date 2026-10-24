#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_RAPI_ACTIVITY_LED)
#undef ENABLE_DEBUG
#endif

#include "emonesp.h"
#include "rapi_activity_led.h"

#ifdef ENABLE_RAPI_ACTIVITY_LED

#include "debug.h"
#include "evse_man.h"

RapiActivityLed rapiActivityLed(RAPI_PORT);

RapiActivityLed::RapiActivityLed(Stream &port) :
  _port(port),
  _evse(NULL),
  _tx_off_at(0),
  _rx_off_at(0),
  _tx_lit(false),
  _rx_lit(false)
{
}

void RapiActivityLed::setup()
{
  pinMode(RAPI_TX_LED, OUTPUT);
  pinMode(RAPI_RX_LED, OUTPUT);
  digitalWrite(RAPI_TX_LED, LOW);
  digitalWrite(RAPI_RX_LED, LOW);
}

void RapiActivityLed::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
  DBUGF("RapiActivityLed: tx=%d rx=%d", RAPI_TX_LED, RAPI_RX_LED);
}

void RapiActivityLed::trigger(uint8_t pin, unsigned long &off_at, bool &lit)
{
  // Light it from the byte rather than on the next tick. A poll cycle is a
  // handful of bytes and what the operator is looking for is the edge, so
  // 10 ms of latency on a 30 ms pulse is worth avoiding for free.
  digitalWrite(pin, HIGH);
  off_at = millis() + RAPI_ACTIVITY_LED_BLINK_MS;
  lit = true;
}

int RapiActivityLed::read()
{
  int data = _port.read();
  if(data >= 0) {
    trigger(RAPI_RX_LED, _rx_off_at, _rx_lit);
  }
  return data;
}

size_t RapiActivityLed::write(uint8_t data)
{
  trigger(RAPI_TX_LED, _tx_off_at, _tx_lit);
  return _port.write(data);
}

size_t RapiActivityLed::write(const uint8_t *buffer, size_t size)
{
  if(size > 0) {
    trigger(RAPI_TX_LED, _tx_off_at, _tx_lit);
  }
  return _port.write(buffer, size);
}

unsigned long RapiActivityLed::loop(MicroTasks::WakeReason reason)
{
  unsigned long now = millis();

  if(_tx_lit && (long)(now - _tx_off_at) >= 0) {
    _tx_lit = false;
  }
  if(_rx_lit && (long)(now - _rx_off_at) >= 0) {
    _rx_lit = false;
  }

  // Solid on outranks the activity blink. The gateway keeps transmitting into
  // a dead controller, so an unanswered bus would otherwise be indistinguishable
  // from an idle one -- the single state most worth seeing across a room. A
  // NULL _evse means begin() has not run yet, which is equally "link not up".
  bool fault = (NULL == _evse) || !_evse->isConnected();

  digitalWrite(RAPI_TX_LED, _tx_lit ? HIGH : LOW);
  digitalWrite(RAPI_RX_LED, (fault || _rx_lit) ? HIGH : LOW);

  return RAPI_ACTIVITY_LED_TICK_MS;
}

#endif // ENABLE_RAPI_ACTIVITY_LED
