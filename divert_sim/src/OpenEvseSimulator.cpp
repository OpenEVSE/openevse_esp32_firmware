
#include "debug.h"

#include "OpenEvseSimulator.h"

#include "openevse.h"

OpenEvseSimulator::OpenEvseSimulator() :
      MicroTasks::Task(),
      _replyQueue(),
      _stream(),
      _pilot(32),
      _state(OPENEVSE_STATE_CONNECTED)
{
}

unsigned long OpenEvseSimulator::loop(MicroTasks::WakeReason reason)
{
//  DBUG("OpenEvseSimulator woke: ");
//  DBUGLN(WakeReason_Scheduled == reason ? "WakeReason_Scheduled" :
//        WakeReason_Event == reason ? "WakeReason_Event" :
//        WakeReason_Message == reason ? "WakeReason_Message" :
//        WakeReason_Manual == reason ? "WakeReason_Manual" :
//        "UNKNOWN");

  if(!_replyQueue.empty())
  {
    std::string reply = _replyQueue.front();
    _replyQueue.pop();

    DBUGF("OpenEvseSimulator::loop reply: %s", reply.c_str());
    _stream.internalWrite(reply);
  }

  if(_stream.internalAvailable())
  {
    std::string cmd = _stream.internalRead();
    DBUGF("OpenEvseSimulator::loop cmd: %s", cmd.c_str());
    processCommand(cmd);
  }

  return 0;
}

void OpenEvseSimulator::sendReply(const char *reply)
{
  DBUGF("OpenEvseSimulator::sendReply: %s", reply);

  std::string str(reply);

  // TODO: add checksum
  str += "\r";

  _replyQueue.push(str);
}

void OpenEvseSimulator::processCommand(std::string &cmd)
{
  DBUGF("OpenEvseSimulator::processCommand: %s", cmd.c_str());

  static char buffer[1024] = "$NK";

  switch (cmd[1])
  {
    case 'G':
      switch(cmd[2])
      {
        case 'E':
        {
          snprintf(buffer, sizeof(buffer), "$OK %ld 0", _pilot);
        } break;

        case 'D':
        {
          snprintf(buffer, sizeof(buffer), "$OK 0 0 0 0");
        } break;
        case 'G':
        {
          snprintf(buffer, sizeof(buffer), "$OK 0");
        } break;
        case 'V':
        {
          snprintf(buffer, sizeof(buffer), "$OK 8.2.2 5.2.1");
        } break;
        case 'F':
        {
          snprintf(buffer, sizeof(buffer), "$OK 0 0 0 0");
        } break;
        case 'C':
        {
          snprintf(buffer, sizeof(buffer), "$OK 6 32 %ld 32", _pilot);
        } break;
        case 'A':
        {
          snprintf(buffer, sizeof(buffer), "$OK 0 220 0");
        } break;
        case 'I':
        {
          snprintf(buffer, sizeof(buffer), "$OK Y57414FF020F0C");
        } break;

        case 'S':
        {
          // TODO: The second state not reflect the Sleep/Disabled state
          snprintf(buffer, sizeof(buffer), "$OK %02lx 0 %02lx 0200", _state, _state);

        } break;
        case 'P':
        {
          snprintf(buffer, sizeof(buffer), "$OK 200 -2560 -2560");
        } break;
        case 'U':
        {
          snprintf(buffer, sizeof(buffer), "$OK 0 0");
        } break;

        default:
          DBUGF("Unhandled get command: %s", cmd.c_str());
          break;
      } break;

    case 'S':
      switch(cmd[2])
      {
        case 'C':
        {
          sscanf(cmd.c_str(), "$SC %ld V", &_pilot);
          snprintf(buffer, sizeof(buffer), "$OK");
        } break;

        case 'Y':
        {
          snprintf(buffer, sizeof(buffer), "$OK");
        } break;

        case 'B':
        {
          snprintf(buffer, sizeof(buffer), "$OK");
        } break;

        default:
          DBUGF("Unhandled set command: %s", cmd.c_str());
          break;
      } break;

    case 'F':
      switch(cmd[2])
      {
        case 'E':
        {
          _state = OPENEVSE_STATE_CHARGING;
          snprintf(buffer, sizeof(buffer), "$OK");
        } break;

        case 'D':
        {
          _state = OPENEVSE_STATE_DISABLED;
          snprintf(buffer, sizeof(buffer), "$OK");
        } break;

        case 'S':
        {
          _state = OPENEVSE_STATE_SLEEPING;
          snprintf(buffer, sizeof(buffer), "$OK");
        } break;

        default:
          DBUGF("Unhandled function command: %s", cmd.c_str());
          break;
      } break;

    default:
      break;
  }

  sendReply(buffer);
}
