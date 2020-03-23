#pragma once
#include <Stream.h>
#include <functional>

#include "queue.h"

// only enable if RAPI ver
#define RAPI_SEQUENCE_ID

#define RAPI_INVALID_SEQUENCE_ID 0

#define RAPI_TIMEOUT_MS 500
#define RAPI_READ_TIMEOUT_MS 20
#define RAPI_BUFLEN 40
#define RAPI_MAX_TOKENS 10

#define ESRAPI_SOC '$' // start of command
#define ESRAPI_EOC 0xd // CR end of command
#define ESRAPI_SOS ':' // start of sequence id

#ifndef RAPI_MAX_COMMANDS
#define RAPI_MAX_COMMANDS 10
#endif

#define RAPI_RESPONSE_QUEUE_FULL             -3
#define RAPI_RESPONSE_BUFFER_OVERFLOW        -2
#define RAPI_RESPONSE_TIMEOUT                -1
#define RAPI_RESPONSE_OK                      0
#define RAPI_RESPONSE_NK                      1
#define RAPI_RESPONSE_INVALID_RESPONSE        2
#define RAPI_RESPONSE_CMD_TOO_LONG            3
#define RAPI_RESPONSE_BAD_CHECKSUM            4
#define RAPI_RESPONSE_BAD_SEQUENCE_ID         5
#define RAPI_RESPONSE_ASYNC_EVENT             6
#define RAPI_RESPONSE_FEATURE_NOT_SUPPORTED   7

// _flags
#define RSF_SEQUENCE_ID_ENABLED   0x01

typedef std::function<void()> RapiEventHandler;

/*
 * return values:
 * See RAPI_RESPONSE_XXXX
*/
typedef std::function<void(int result)> RapiCommandCompleteHandler;

struct CommandItem {
  String command;
  RapiCommandCompleteHandler handler;
  unsigned int timeout;
};

class RapiSender {
private:
  Stream *_stream;
  uint32_t _sent;
  uint32_t _success;
  bool _connected;
  uint8_t _sequenceId;
  uint8_t _flags;
  int _tokenCnt;
  char *_tokens[RAPI_MAX_TOKENS];
  RapiEventHandler _onRapiEvent;

  Queue<CommandItem> _commandQueue;
  RapiCommandCompleteHandler _completeHandler;
  uint32_t _timeout;
  bool _waitingForReply;

  char _respBuf[RAPI_BUFLEN];
  char _respBufOrig[RAPI_BUFLEN];

  int _tokenize();
  void _sendNextCmd();
  void _sendCmd(const char *cmdstr);
  void _sendTail(uint8_t chk);
  int _waitForResult(unsigned long timeout);
  void _commandComplete(int result);
  uint8_t _sequenceIdEnabled() {
    return (_flags & RSF_SEQUENCE_ID_ENABLED) ? 1 : 0;
  }
public:

  RapiSender(Stream *stream);
  void setStream(Stream *stream) { _stream = stream; }
  //  void sendString(const char *str) { dbgprint(str); }

  void sendCmd(const char *cmdstr, RapiCommandCompleteHandler callback=nullptr, unsigned long timeout=RAPI_TIMEOUT_MS);
  void sendCmd(String &cmdstr, RapiCommandCompleteHandler callback=nullptr, unsigned long timeout=RAPI_TIMEOUT_MS);
  void sendCmd(const __FlashStringHelper *cmdstr, RapiCommandCompleteHandler callback=nullptr, unsigned long timeout=RAPI_TIMEOUT_MS);

  int sendCmdSync(const char *cmdstr, unsigned long timeout=RAPI_TIMEOUT_MS);
  int sendCmdSync(String &cmdstr, unsigned long timeout=RAPI_TIMEOUT_MS);
  int sendCmdSync(const __FlashStringHelper *cmdstr, unsigned long timeout=RAPI_TIMEOUT_MS);

  void enableSequenceId(uint8_t tf);
  int8_t getTokenCnt() { return _tokenCnt; }
  const char *getResponse() { return _respBufOrig; }
  const char *getToken(int i) {
    if (i < _tokenCnt) return _tokens[i];
    else return NULL;
  }
  void setOnEvent(RapiEventHandler callback) {
    _onRapiEvent = callback;
  }

  uint32_t getSent() {
    return _sent;
  }
  uint32_t getSuccess() {
    return _success;
  }
  bool isConnected() {
    return _connected;
  }

  void loop();
};

