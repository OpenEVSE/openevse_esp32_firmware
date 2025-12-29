#pragma once

// Minimal host-build stub for ArduinoOTA.
// Used only for PlatformIO `native` builds.

typedef int ota_error_t;

class ArduinoOTAClass {
 public:
  ArduinoOTAClass() = default;

  template <typename Fn>
  ArduinoOTAClass& onStart(Fn /*fn*/) { return *this; }
  template <typename Fn>
  ArduinoOTAClass& onEnd(Fn /*fn*/) { return *this; }
  template <typename Fn>
  ArduinoOTAClass& onProgress(Fn /*fn*/) { return *this; }
  template <typename Fn>
  ArduinoOTAClass& onError(Fn /*fn*/) { return *this; }

  void setHostname(const char* /*hostname*/) {}
  void begin() {}
  void handle() {}
};

extern ArduinoOTAClass ArduinoOTA;
