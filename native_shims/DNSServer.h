#pragma once

// Minimal host-build stub for ESP8266/ESP32 DNSServer.
// Used only for PlatformIO `native` builds.

enum class DNSReplyCode {
  NoError = 0,
};

class DNSServer {
 public:
  DNSServer() = default;

  void setErrorReplyCode(DNSReplyCode /*code*/) {}

  bool start(unsigned short /*port*/, const char* /*domainName*/, unsigned long /*resolvedIP*/) { return true; }
  bool start(unsigned short /*port*/, const char* /*domainName*/, const void* /*resolvedIP*/) { return true; }
  void stop() {}
  void processNextRequest() {}
};
