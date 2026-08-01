#ifndef _DIVERT_SIM_SIM_STREAM_H
#define _DIVERT_SIM_SIM_STREAM_H

#include <Stream.h>

class SimEvse;

// A no-op Stream subclass used solely as a transport for a per-EvseManager
// `SimEvse*` pointer. The simulator's `RapiSender.cpp` casts its `_stream`
// member to `SimStream*` to find the SimEvse it should read/write.
//
// All Stream methods are stubbed because the simulator's `RapiSender`
// short-circuits the actual serial path (it builds canned token responses
// directly).
class SimStream : public Stream
{
public:
  SimEvse *sim = nullptr;

  // Stream interface — all no-ops.
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 1; }
  void flush() override {}
};

#endif // _DIVERT_SIM_SIM_STREAM_H
