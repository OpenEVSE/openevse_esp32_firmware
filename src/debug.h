#ifndef __DEBUG_H
#define __DEBUG_H

//#define ENABLE_DEBUG
//#define DEBUG_PORT Serial

#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)

#ifdef ENABLE_DEBUG

#ifdef DEBUG_SERIAL1
#error DEBUG_SERIAL1 defiend, please use -DDEBUG_PORT=Serial1 instead
#endif

#ifndef DEBUG_PORT
#define DEBUG_PORT Serial1
#endif
#define DEBUG DEBUG_PORT

// Use os_printf, works but also outputs additional dubug if not using Serial
//#define DEBUG_BEGIN(speed)  DEBUG_PORT.begin(speed); DEBUG_PORT.setDebugOutput(true)
//#define DBUGF(format, ...)  os_printf(PSTR(format "\n"), ##__VA_ARGS__)

// Serial.printf_P needs Git version of Arduino Core
//#define DEBUG_BEGIN(speed)  DEBUG_PORT.begin(speed)
//#define DBUGF(format, ...)  DEBUG_PORT.printf_P(PSTR(format "\n"), ##__VA_ARGS__)

#define DEBUG_BEGIN(speed)  DEBUG_PORT.begin(speed)
#define DBUGF(format, ...)  DEBUG_PORT.printf(format "\n", ##__VA_ARGS__)

#define DBUG(...)           DEBUG_PORT.print(__VA_ARGS__)
#define DBUGLN(...)         DEBUG_PORT.println(__VA_ARGS__)

#else

#define DEBUG_BEGIN(speed)
#define DBUGF(...)
#define DBUG(...)
#define DBUGLN(...)

#ifdef DEBUG_SERIAL1
#define DEBUG Serial1
#else
#define DEBUG Serial
#endif

#endif // DEBUG

#endif // __DEBUG_H
