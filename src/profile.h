#ifndef __PROFILE_H
#define __PROFILE_H

#if defined(ENABLE_PROFILE)

#if defined(ENABLE_NOISY_PROFILE)
#define Profile_Start(x) \
  unsigned long profile ## x = millis(); \
  DBUGLN(">> Start " #x)

#define Profile_End(x, max) \
  unsigned long profile ## x ## Diff = millis() - profile ## x; \
  DBUGF(">> End " #x " %ldms", profile ## x ## Diff);\

#else

#define Profile_Start(x) \
  unsigned long profile ## x = millis()

#define Profile_End(x, max) \
  unsigned long profile ## x ## Diff = millis() - profile ## x; \
  if(profile ## x ## Diff > max) { \
    DBUGF(">> Slow " #x " %ldms", profile ## x ## Diff);\
  }

#endif

#else // ENABLE_PROFILE

#define Profile_Start(x)
#define Profile_End(x, min)

#endif // ENABLE_PROFILE

#endif
