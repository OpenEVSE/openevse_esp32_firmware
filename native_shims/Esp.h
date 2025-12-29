#pragma once

// Shim Esp.h for EpoxyDuino host builds.
// Adds the ESP32-only getSdkVersion() used by the firmware.

#include <sys/time.h>
#include <stdint.h>
#include <epoxy_test/injection>
#include "Stream.h"

class EspClass
{
  public:
    EspClass() {
      gettimeofday(&start, NULL);
    }

    void reset()
    {
      EpoxyTest::reset();
    };

    // Very ugly approximation, this is freeStack
    unsigned long getFreeHeap() {
      int i;
      static int* h=40000+&i;
      return h-&i;
    }

    uint32_t getCpuFreqMHZ() { return 80; }

    uint32_t getChipId() { return 0; }

    uint64_t getEfuseMac() { return 0; }

    uint32_t getFlashChipSize() { return 4u * 1024u * 1024u; }

    uint32_t getCycleCount() {
      struct timeval now;
      gettimeofday(&now, NULL);
      return getCpuFreqMHZ()
          * ((now.tv_sec-start.tv_sec)*1000000+(now.tv_usec-start.tv_usec));
    }

    const char* getSdkVersion() { return "epoxy"; }

    void restart() { reset(); };

  private:
    struct timeval start;
};

class Serial_ : public Stream
{
};

extern EspClass ESP;
