#pragma once

// Minimal host-build stub for ESPmDNS.
// Used only for PlatformIO `native` builds.

#include <Arduino.h>

class MDNSResponder {
 public:
  bool begin(const char* /*host*/) { return true; }
  void end() {}
  void addService(const char* /*name*/, const char* /*proto*/, int /*port*/) {}

  void addServiceTxt(const char* /*name*/, const char* /*proto*/, const char* /*key*/, const char* /*value*/) {}
  void addServiceTxt(const char* /*name*/, const char* /*proto*/, const char* /*key*/, const String& /*value*/) {}
};

extern MDNSResponder MDNS;
