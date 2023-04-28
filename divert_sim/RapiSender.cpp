#include <Arduino.h>
#include "RapiSender.h"
#include "openevse.h"
#include "input.h"

#define dbgprint(s) DBUG(s)
#define dbgprintln(s) DBUGLN(s)
#ifdef ENABLE_DEBUG
#define DBG
#endif

extern long pilot;
extern long state;

static CommandItem commandQueueItems[RAPI_MAX_COMMANDS];

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
}

// return = 0 = OK
//        = 1 = command will cause buffer overflow
void
RapiSender::_sendCmd(const char *cmdstr) {
}

void RapiSender::_sendTail(uint8_t chk) {
}

// return = 0 = OK
//        = 1 = bad checksum
//        = 2 = bad sequence id
int
RapiSender::_tokenize() {
  return RAPI_RESPONSE_OK;
}

void RapiSender::_commandComplete(int result)
{
}

void
RapiSender::sendCmd(const char *cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout)
{
  String cmd = cmdstr;
  return sendCmd(cmd, callback, timeout);
}

void
RapiSender::sendCmd(String &cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout)
{
  int ret = sendCmdSync(cmdstr, timeout);
  callback(ret);
}

void
RapiSender::sendCmd(const __FlashStringHelper *cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout)
{
  String cmd = cmdstr;
  return sendCmd(cmd, callback, timeout);
}

int
RapiSender::sendCmdSync(const char *cmdstr, unsigned long timeout) {
  String cmd = cmdstr;
  return sendCmdSync(cmd, timeout);
}

int
RapiSender::sendCmdSync(String &cmd, unsigned long timeout)
{
  DBUGVAR(cmd);

  static char ok[] = "$OK";
  static char zero[] = "0";
  static char buf1[32];

  switch (cmd[1])
  {
    case 'G':
      switch(cmd[2])
      {
        case 'E':
        {
          sprintf(buf1, "%ld", pilot);
          _tokens[0] = ok;
          _tokens[1] = buf1;
          _tokens[2] = zero;
          _tokenCnt = 3;
        } break;

        case 'D':
        {
          _tokens[0] = ok;
          _tokens[1] = zero;
          _tokens[2] = zero;
          _tokens[3] = zero;
          _tokens[4] = zero;
          _tokenCnt = 5;
        } break;
        case 'G':
        {
          _tokens[0] = ok;
          _tokens[1] = zero;
          _tokenCnt = 2;
        } break;
        case 'V':
        {
          _tokens[0] = ok;
          _tokens[1] = "1.2.3";
          _tokens[2] = "1.2.3";
          _tokenCnt = 3;
        } break;
        case 'F':
        {
          _tokens[0] = ok;
          _tokens[1] = zero;
          _tokens[2] = zero;
          _tokens[3] = zero;
          _tokenCnt = 4;
        } break;
        case 'C':
        {
          sprintf(buf1, "%ld", pilot);
          _tokens[0] = ok;
          _tokens[1] = "6";
          _tokens[2] = "32";
          _tokens[3] = buf1;
          _tokens[4] = "32";
          _tokenCnt = 5;
        } break;
        case 'A':
        {
          _tokens[0] = ok;
          _tokens[1] = "220";
          _tokens[2] = zero;
          _tokenCnt = 3;
        } break;
        case 'I':
        {
          _tokens[0] = ok;
          _tokens[1] = "Y57414FF020F0C";
          _tokenCnt = 2;
        } break;
        case 'S':
        {
          char *ptr = buf1;

          _tokens[0] = ok;
          _tokens[1] = ptr;
          ptr += sprintf(ptr, "%ld", state) + 1;
          _tokens[2] = zero;
          _tokens[3] = ptr;
          ptr += sprintf(ptr, "%ld", state) + 1; // Should not reflect the Sleep/Disabled state
          _tokens[4] = zero;
          _tokenCnt = 5;
        } break;
        case 'P':
        {
          _tokens[0] = ok;
          _tokens[1] = "200";
          _tokens[2] = "-2560";
          _tokens[3] = "-2560";
          _tokenCnt = 4;
        } break;
        case 'U':
        {
          _tokens[0] = ok;
          _tokens[1] = zero;
          _tokens[2] = zero;
          _tokenCnt = 3;
        } break;

        default:
          DBUGF("Unhandled get command: %s", cmd.c_str());
          return RAPI_RESPONSE_NK;
      } break;

    case 'S':
      switch(cmd[2])
      {
        case 'C':
        {
          sscanf(cmd.c_str(), "$SC %ld V", &pilot);
          _tokens[0] = ok;
          _tokenCnt = 1;
        } break;
        case 'Y':
        {
          _tokens[0] = ok;
          _tokenCnt = 1;
        } break;
        case 'B':
        {
          _tokens[0] = ok;
          _tokenCnt = 1;
        } break;

        default:
          DBUGF("Unhandled set command: %s", cmd.c_str());
          return RAPI_RESPONSE_NK;
      } break;

    case 'F':
      switch(cmd[2])
      {
        case 'E':
        {
          state = OPENEVSE_STATE_CHARGING;
          _tokens[0] = ok;
          _tokenCnt = 1;
        } break;
        case 'D':
        {
          state = OPENEVSE_STATE_DISABLED;
          _tokens[0] = ok;
          _tokenCnt = 1;
        } break;
        case 'S':
        {
          state = OPENEVSE_STATE_SLEEPING;
          _tokens[0] = ok;
          _tokenCnt = 1;
        } break;

        default:
          DBUGF("Unhandled function command: %s", cmd.c_str());
          return RAPI_RESPONSE_NK;
      } break;

    default:
      break;
  }


  return RAPI_RESPONSE_OK;
}

int
RapiSender::sendCmdSync(const __FlashStringHelper *cmdstr, unsigned long timeout) {
//  String cmd;
//  cmd.concat(cmdstr);
//  return sendCmdSync(cmd, timeout);
  return RAPI_RESPONSE_OK;
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
  return RAPI_RESPONSE_OK;
}

void
RapiSender::enableSequenceId(uint8_t tf) {
}

void
RapiSender::loop()
{
}
