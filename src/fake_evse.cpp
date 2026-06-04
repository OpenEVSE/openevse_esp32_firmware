#include "fake_evse.h"
#ifdef FAKE_EVSE

size_t FakeEvseStream::write(uint8_t c) {
  if (c == '\r') {            // RapiSender terminates commands with CR
    handleLine();
    _in = "";
  } else if (c != '\n') {
    _in += (char)c;
  }
  return 1;
}

size_t FakeEvseStream::write(const uint8_t *buf, size_t size) {
  for (size_t i = 0; i < size; i++) write(buf[i]);
  return size;
}

int FakeEvseStream::read() {
  if (_outPos >= _out.length()) return -1;
  char c = _out[_outPos++];
  if (_outPos >= _out.length()) { _out = ""; _outPos = 0; }  // compact when drained
  return (uint8_t)c;
}

int FakeEvseStream::peek() {
  if (_outPos >= _out.length()) return -1;
  return (uint8_t)_out[_outPos];
}

void FakeEvseStream::handleLine() {
  // Strip "^CK" checksum suffix (we don't validate the firmware's outgoing csum).
  String body = _in;
  int caret = body.indexOf('^');
  if (caret >= 0) body = body.substring(0, caret);
  if (body.length() == 0) return;
  enqueue(fake_evse_handle(state, std::string(body.c_str())));
}

void FakeEvseStream::tick(double seconds) {
  enqueue(fake_evse_tick(state, seconds));
}
#endif // FAKE_EVSE
