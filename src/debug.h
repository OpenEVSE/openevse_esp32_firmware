#ifndef __DEBUG_H
#define __DEBUG_H

#include "MicroDebug.h"

#ifndef UNIT_TEST

#include "StreamSpy.h"

extern StreamSpy SerialDebug;
extern StreamSpy SerialEvse;

#undef DEBUG_PORT
#define DEBUG_PORT SerialDebug

#undef RAPI_PORT
#define RAPI_PORT SerialEvse

extern void debug_setup();

#endif // UNIT_TEST

#endif // __DEBUG_H
