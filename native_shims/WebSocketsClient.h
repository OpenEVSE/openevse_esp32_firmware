#pragma once

// Minimal host-build stub for WebSocketsClient.
// Used only for PlatformIO `native` builds.

class WebSocketsClient {
 public:
  WebSocketsClient() = default;
  void begin(const char* /*host*/, int /*port*/, const char* /*url*/ = "/") {}
  void loop() {}
};
