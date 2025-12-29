#pragma once

// Misc ESP32-only helpers referenced by the firmware.
// Used only for PlatformIO `native` builds.

#pragma once

// Pull in system time headers before defining any compatibility macros.
#include <time.h>
#include <sys/time.h>

static inline void enableLoopWDT() {}
static inline void feedLoopWDT() {}

// Arduino pin->interrupt mapping helper (used by MicroTasks). For host builds
// we treat the pin number as the interrupt number.
#ifndef digitalPinToInterrupt
#define digitalPinToInterrupt(p) (p)
#endif

// ESP32 Arduino code often uses `timezone` as a typedef. On Linux/glibc this
// is typically a variable, while the type is `struct timezone`.
// Map `timezone` token to `struct timezone` for host builds.
#ifndef timezone
#define timezone struct timezone
#endif
