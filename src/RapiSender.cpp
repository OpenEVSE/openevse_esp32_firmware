#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_RAPI)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include "RapiSender.h"
#include "debug.h"

#define dbgprint(s) DBUG(s)
#define dbgprintln(s) DBUGLN(s)
#ifdef ENABLE_DEBUG
#define DBG
#endif

static CommandItem commandQueueItems[RAPI_MAX_COMMANDS];

// convert 2-digit hex string to uint8_t
uint8_t
htou8(const char *s) {
  uint8_t u = 0;
  for (int i = 0; i < 2; i++) {
    char c = s[i];
    if (c != '\0') {
      if (i == 1)
        u <<= 4;
      if ((c >= '0') && (c <= '9')) {
        u += c - '0';
      } else if ((c >= 'A') && (c <= 'F')) {
        u += c - 'A' + 10;
      }
      //      else if ((c >= 'a') && (c <= 'f')) {
      //        u += c - 'a' + 10;
      //      }
      else {
        // invalid character received
        return 0;
      }
    }
  }
  return u;
}

RapiSender::RapiSender(Stream * stream) :
  _stream(stream),
  _sent(0),
  _success(0),
  _connected(false),
  _sequenceId(RAPI_INVALID_SEQUENCE_ID),
  _flags(0),
  _tokenCnt(0),
  _tokens{},
  _onRapiEvent(nullptr),
  _commandQueue(commandQueueItems, RAPI_MAX_COMMANDS),
  _completeHandler(nullptr),
  _timeout(0),
  _waitingForReply(false),
  _respBuf{},
  _respBufOrig{}
{
}

void RapiSender::_sendNextCmd()
{
  CommandItem cmd;
  if(_commandQueue.pop(cmd))
  {
    _sendCmd(cmd.command.c_str());
    _completeHandler = cmd.handler;
    _timeout = millis() + cmd.timeout;
    _waitingForReply = true;
  }
}

// return = 0 = OK
//        = 1 = command will cause buffer overflow
void
RapiSender::_sendCmd(const char *cmdstr) {
  _stream->print(cmdstr);
  dbgprint(cmdstr);

  const char *s = cmdstr;
  uint8_t chk = 0;
  while (*s) {
    chk ^= *(s++);
  }

  _sendTail(chk);

  _sent++;
}

void RapiSender::_sendTail(uint8_t chk) {
  if (_sequenceIdEnabled()) {
    if (++_sequenceId == RAPI_INVALID_SEQUENCE_ID)
      ++_sequenceId;
    sprintf(_respBuf, " %c%02X", ESRAPI_SOS, (unsigned) _sequenceId);
    const char *s = _respBuf;
    while (*s) {
      chk ^= *(s++);
    }
    _stream->print(_respBuf);
    dbgprint(_respBuf);
  }

  sprintf(_respBuf, "^%02X%c", (unsigned) chk, ESRAPI_EOC);
  _stream->print(_respBuf);
  dbgprintln(_respBuf);
  _stream->flush();

  *_respBuf = 0;
}

// return = 0 = OK
//        = 1 = bad checksum
//        = 2 = bad sequence id
int
RapiSender::_tokenize() {
  uint8_t chk = 0;
  char *s = _respBuf;
  dbgprint("resp: ");
  dbgprintln(_respBuf);

  while (*s != '^' && *s != '\0') {
    chk ^= *(s++);
  }
  if (*s == '^') {
    uint8_t rchk = htou8(s + 1);
    if (rchk != chk) {
      _tokenCnt = 0;
#ifdef DBG
      sprintf(_respBuf, "bad chk %x %x %s", rchk, chk, s);
      dbgprintln(_respBuf);
#endif
      return RAPI_RESPONSE_BAD_CHECKSUM;
    }
    *s = '\0';
  }

  _tokenCnt = 0;
  s = _respBuf;
  while (*s) {
    _tokens[_tokenCnt++] = s++;
    if (_tokenCnt == RAPI_MAX_TOKENS)
      break;

    while (*s && (*s != ' ') && (*s != ESRAPI_SOS)) {
      s++;
    }
    if (*s == ' ') {
      *(s++) = '\0';
    } else if (*s == ESRAPI_SOS) {
      *(s++) = '\0';
      uint8_t seqid = htou8(s + 1);
      if (seqid != _sequenceId) {
#ifdef DBG
        sprintf(_respBuf, "bad seqid %x %x %s", seqid, _sequenceId, s);
        dbgprintln(_respBuf);
#endif
        _tokenCnt = 0;
        return RAPI_RESPONSE_BAD_SEQUENCE_ID;
      }
      break;                    // sequence id is last - break out
    }
  }

  return RAPI_RESPONSE_OK;
}

void RapiSender::_commandComplete(int result)
{
  if(_waitingForReply) {
    if(nullptr != _completeHandler) {
      _completeHandler(result);
    }
    _waitingForReply = false;
  }
  _sendNextCmd();
}

void
RapiSender::sendCmd(const char *cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout) {
  String cmd = cmdstr;
  return sendCmd(cmd, callback, timeout);
}

void
RapiSender::sendCmd(String &cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout) {
  CommandItem cmd = {
    cmdstr,
    callback,
    timeout
  };
  if(_commandQueue.push(cmd)) {
    if(!_waitingForReply) {
      _sendNextCmd();
    }
  } else if(nullptr != callback) {
    callback(RAPI_RESPONSE_QUEUE_FULL);
  }
}

void
RapiSender::sendCmd(const __FlashStringHelper *cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout) {
  String cmd = cmdstr;
  return sendCmd(cmd, callback, timeout);
}

int
RapiSender::sendCmdSync(const char *cmdstr, unsigned long timeout) {
  String cmd = cmdstr;
  return sendCmdSync(cmd, timeout);
}

int
RapiSender::sendCmdSync(String &cmdstr, unsigned long timeout)
{
  struct SendCmdSyncData
  {
    int ret;
    bool finished;
  } resultData = {0, false};
  SendCmdSyncData *result = &resultData;

  sendCmd(cmdstr, [result](int ret) {
    result->ret = ret;
    result->finished = true;
  });

  while(!result->finished) {
    loop();
  }

  return result->ret;
}

int
RapiSender::sendCmdSync(const __FlashStringHelper *cmdstr, unsigned long timeout) {
  String cmd = cmdstr;
  return sendCmdSync(cmd, timeout);
}

/*
 * return values:
 * -1= timeout
 * 0= success
 * 1=$NK
 * 2=invalid RAPI response
 * 3=cmdstr too long
 * 4=bad checksum
 * 5=bad sequence ID
*/
int
RapiSender::_waitForResult(unsigned long timeout) {
  unsigned long mss = millis();

  _tokenCnt = 0;
  *_respBuf = 0;
  int bufpos = 0;
  do {
    int bytesavail = _stream->available();
    if (bytesavail) {
      for (int i = 0; i < bytesavail; i++) {
        char c = _stream->read();
        if (!bufpos && (c != ESRAPI_SOC)) {
          // wait for start character
          continue;
        } else if (bufpos && (c == ESRAPI_EOC)) {
          _respBuf[bufpos] = '\0';
          // Save the original response
          strncpy(_respBufOrig, _respBuf, RAPI_BUFLEN);
          int ret = _tokenize();
          if (RAPI_RESPONSE_OK == ret)
            break;

          return ret;
        } else {
          _respBuf[bufpos++] = c;
          if (bufpos >= (RAPI_BUFLEN - 1))
            return RAPI_RESPONSE_BUFFER_OVERFLOW;
        }
      }
    }
  } while (!_tokenCnt && ((millis() - mss) < timeout));

#ifdef DBG
  dbgprint("TOKENCNT: ");
  dbgprintln(_tokenCnt);
  for (int i = 0; i < _tokenCnt; i++) {
    dbgprintln(_tokens[i]);
  }
  dbgprintln("");
#endif

  if (_tokenCnt > 0) {
    if (!strcmp(_tokens[0], "$OK")) {
      _success++;
      _connected = true;
      return RAPI_RESPONSE_OK;
    } else if (!strcmp(_tokens[0], "$NK")) {
      return RAPI_RESPONSE_NK;
    } else if (!strcmp(_tokens[0],"$WF") ||
               !strcmp(_tokens[0],"$ST") ||
               !strncmp(_tokens[0],"$A",2))
    {
      return RAPI_RESPONSE_ASYNC_EVENT;
    } else { // not OK or NK
      return RAPI_RESPONSE_INVALID_RESPONSE;
    }
  } else { // !_tokenCnt
    _connected = false;
    return RAPI_RESPONSE_TIMEOUT;
  }
}

void
RapiSender::enableSequenceId(uint8_t tf) {
  if (tf) {
    _sequenceId = (uint8_t) millis();   // seed with random number
    _flags |= RSF_SEQUENCE_ID_ENABLED;
  } else {
    _sequenceId = RAPI_INVALID_SEQUENCE_ID;
    _flags &= ~RSF_SEQUENCE_ID_ENABLED;
  }
}

void
RapiSender::loop()
{
  if(_stream->available())
  {
    int ret = _waitForResult(RAPI_READ_TIMEOUT_MS);
    if(RAPI_RESPONSE_ASYNC_EVENT == ret) {
      // async EVSE state transition or WiFi event
      if(nullptr != _onRapiEvent) {
        _onRapiEvent();
      }
    } else {
      _commandComplete(ret);
    }
  }
  else if (_waitingForReply && millis() >= _timeout)
  {
    _commandComplete(RAPI_RESPONSE_TIMEOUT);
  }
}
