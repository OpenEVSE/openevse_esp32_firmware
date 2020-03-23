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
RapiSender::sendCmd(const char *cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout) {
}

void
RapiSender::sendCmd(String &cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout) {
}

void
RapiSender::sendCmd(const __FlashStringHelper *cmdstr, RapiCommandCompleteHandler callback, unsigned long timeout) {
}

int
RapiSender::sendCmdSync(const char *cmdstr, unsigned long timeout) {
  String cmd = cmdstr;
  return sendCmdSync(cmd, timeout);
}

int
RapiSender::sendCmdSync(String &cmdstr, unsigned long timeout)
{
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
