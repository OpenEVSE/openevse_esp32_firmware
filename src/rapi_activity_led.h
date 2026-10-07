#ifndef _OPENEVSE_RAPI_ACTIVITY_LED_H
#define _OPENEVSE_RAPI_ACTIVITY_LED_H

// Discrete RAPI traffic LEDs, as fitted on the ESP32-S3 LCD board from v1.6.1:
// a red on IO45 (TX) and a yellow on IO46 (RX), each driving the anode through
// 1k to GND.
//
// Active HIGH is a hardware constraint, not a style choice. Both are strapping
// pins -- IO45 selects the VDD_SPI voltage and must latch low for 3.3 V, IO46
// gates the ROM boot message -- and an LED to GND cannot pull either above its
// forward voltage, so the weak internal pull-downs still sample 0 at reset.
// Wired the other way (resistor to 3.3 V, cathode on the pin) the board does
// not boot. Never invert the sense and never add an external pull-up.
//
// Boards without the LEDs leave RAPI_TX_LED/RAPI_RX_LED undefined and compile
// none of this; EvseManager then talks to RAPI_PORT directly.

#if defined(RAPI_TX_LED) != defined(RAPI_RX_LED)
#error "RAPI_TX_LED and RAPI_RX_LED must be defined together"
#endif

#if defined(RAPI_TX_LED) && defined(RAPI_RX_LED)
#define ENABLE_RAPI_ACTIVITY_LED
#endif

#ifdef ENABLE_RAPI_ACTIVITY_LED

#include <Arduino.h>
#include <MicroTasks.h>

// Retriggerable one-shot: any byte holds the LED on for this long after the
// last one. Comfortably above the ~20 ms an eye needs, so a single 87 us
// character at 115200 still registers; sustained traffic reads as a steady
// glow, which is the correct appearance. Lighting the raw UART line instead
// would be invisible -- that is why FTDI parts expose TXLED#/RXLED# through a
// one-shot rather than exposing the line itself.
#ifndef RAPI_ACTIVITY_LED_BLINK_MS
#define RAPI_ACTIVITY_LED_BLINK_MS 30
#endif

// The tick only ever has to *end* a one-shot; the byte itself lights the LED.
#ifndef RAPI_ACTIVITY_LED_TICK_MS
#define RAPI_ACTIVITY_LED_TICK_MS 10
#endif

class EvseManager;

// A Stream decorator wrapped around the RAPI port ahead of EvseManager. This
// is the only seam that sees both directions at byte level: RapiSender's
// getSent() counts whole commands and has no RX side at all, and setOnEvent()
// fires on asynchronous events only, missing every command reply.
class RapiActivityLed : public Stream, public MicroTasks::Task
{
  private:
    Stream &_port;
    EvseManager *_evse;

    // A deadline plus an explicit "still lit" flag, rather than comparing
    // against a bare last-byte timestamp: on a board left idle the latter
    // blinks once every 49 days as millis() laps the stored value.
    unsigned long _tx_off_at;
    unsigned long _rx_off_at;
    bool _tx_lit;
    bool _rx_lit;

    void trigger(uint8_t pin, unsigned long &off_at, bool &lit);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    RapiActivityLed(Stream &port);

    void begin(EvseManager &evse);

    // Stream/Print, all forwarded. Everything the RAPI stack does lands in
    // read() or one of the write()s -- readBytes() and print() are built on
    // them -- so there is nowhere else for traffic to escape the count.
    int available() { return _port.available(); }
    int peek() { return _port.peek(); }
    int availableForWrite() { return _port.availableForWrite(); }
    void flush() { _port.flush(); }
    int read();
    size_t write(uint8_t data);
    size_t write(const uint8_t *buffer, size_t size);
    using Print::write;
};

extern RapiActivityLed rapiActivityLed;

#define RAPI_EVSE_STREAM rapiActivityLed

#else /* !ENABLE_RAPI_ACTIVITY_LED */

#define RAPI_EVSE_STREAM RAPI_PORT

#endif

#endif // _OPENEVSE_RAPI_ACTIVITY_LED_H
