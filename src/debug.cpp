#include <StreamSpy.h>

StreamSpy SerialDebug(DEBUG_PORT);
StreamSpy SerialEvse(RAPI_PORT);

void debug_setup()
{
  DEBUG_PORT.begin(115200);
  SerialDebug.begin(2048);

  RAPI_PORT.begin(115200);
  SerialEvse.begin(2048);
}
