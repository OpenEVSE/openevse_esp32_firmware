#pragma once
#ifdef FAKE_EVSE
#include <Arduino.h>
#include <Stream.h>
#include "fake_evse_core.h"

// In-memory Stream standing in for the OpenEVSE controller UART.
// Firmware writes RAPI commands via write(); replies are queued for read().
class FakeEvseStream : public Stream {
public:
  FakeEvseState state;

  // Stream/Print interface
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buf, size_t size) override;
  int available() override {
    return (_outPos < _out.length()) ? (int)(_out.length() - _outPos) : 0;
  }
  int read() override;
  int peek() override;
  void flush() override {}

  // Call ~1 Hz from the main loop to advance the simulation.
  void tick(double seconds);

private:
  String _in;                 // accumulates the current inbound command
  String _out;                // queued reply bytes to be read back
  size_t _outPos = 0;

  void enqueue(const std::string &frame) { if (!frame.empty()) _out += frame.c_str(); }
  void handleLine();          // process one complete "$...^CK" line
};

// The single bench-mode fake controller instance (defined in main.cpp).
extern FakeEvseStream fakeEvseStream;
#endif // FAKE_EVSE
