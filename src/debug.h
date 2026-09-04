#ifndef __DEBUG_H
#define __DEBUG_H

#undef DEBUG_PORT
#define DEBUG_PORT SerialDebug

#undef RAPI_PORT
#define RAPI_PORT SerialEvse

#include "MicroDebug.h"
#include "StreamSpy.h"

extern StreamSpy SerialDebug;
extern StreamSpy SerialEvse;

extern void debug_setup();

#if defined(EPOXY_DUINO)
// Set the RAPI PTY path (must be called before debug_setup())
extern "C" void debug_set_rapi_path(const char* path);
#endif

#endif // __DEBUG_H
