#pragma once
#include <Stream.h>

// only enable if RAPI ver
#define RAPI_SEQUENCE_ID

#define RAPI_INVALID_SEQUENCE_ID 0

#define RAPI_TIMEOUT_MS 500
#define RAPI_BUFLEN 40
#define RAPI_MAX_TOKENS 10

#define ESRAPI_SOC '$' // start of command
#define ESRAPI_EOC 0xd // CR end of command
#define ESRAPI_SOS ':' // start of sequence id

// _flags
#define RSF_SEQUENCE_ID_ENABLED   0x01

typedef void (* fnRapiEvent)();

class RapiSender {
  Stream *_stream;
  uint8_t _sequenceId;
  uint8_t _flags;
  int _tokenCnt;
  char *_tokens[RAPI_MAX_TOKENS];
  fnRapiEvent _onRapiEvent;

  char _respBuf[RAPI_BUFLEN];
  char _respBufOrig[RAPI_BUFLEN];
  int _tokenize();
  void _sendCmd(const char *cmdstr);
  void _sendTail(uint8_t chk);
  int _waitForResult(unsigned long timeout);
  uint8_t _sequenceIdEnabled() {
    return (_flags & RSF_SEQUENCE_ID_ENABLED) ? 1 : 0;
  }

public:

  RapiSender(Stream *stream);
  void setStream(Stream *stream) { _stream = stream; }
  //  void sendString(const char *str) { dbgprint(str); }
  int sendCmd(const char *cmdstr, unsigned long timeout=RAPI_TIMEOUT_MS);
  int sendCmd(String &cmdstr, unsigned long timeout=RAPI_TIMEOUT_MS);
  int sendCmd(const __FlashStringHelper *cmdstr, unsigned long timeout=RAPI_TIMEOUT_MS);
  void enableSequenceId(uint8_t tf);
  int8_t getTokenCnt() { return _tokenCnt; }
  const char *getResponse() { return _respBufOrig; }
  const char *getToken(int i) {
    if (i < _tokenCnt) return _tokens[i];
    else return NULL;
  }
  void setOnEvent(fnRapiEvent callback) {
    _onRapiEvent = callback;
  }
  void loop();
};

