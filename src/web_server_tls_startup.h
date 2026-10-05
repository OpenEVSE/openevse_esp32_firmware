#ifndef WEB_SERVER_TLS_STARTUP_H
#define WEB_SERVER_TLS_STARTUP_H

#include <stdint.h>

/** Result of startup: selected protocol/port, or an inactive listener with port zero. */
struct WebServerListenerState
{
  bool started;
  bool https;
  uint16_t port;
};

/**
 * Attempt TLS startup only when both certificate and key are nonempty.
 * @return The supplied startup callback's result, or false for missing material.
 */
template <typename StartHttps>
bool web_server_start_https(const char *certificate, const char *private_key,
                            StartHttps start_https)
{
  if(nullptr == certificate || '\0' == certificate[0] ||
     nullptr == private_key || '\0' == private_key[0]) {
    return false;
  }

  return start_https(certificate, private_key);
}

/**
 * Select HTTPS after successful startup, otherwise attempt the HTTP fallback.
 * @return The running listener's protocol/port, or an inactive state if both fail.
 */
template <typename StartHttps, typename StartHttp>
WebServerListenerState web_server_start_listeners(
  const char *certificate, const char *private_key,
  uint16_t https_port, uint16_t http_port,
  StartHttps start_https, StartHttp start_http)
{
  if(web_server_start_https(certificate, private_key, start_https)) {
    return {true, true, https_port};
  }

  if(start_http()) {
    return {true, false, http_port};
  }

  return {false, false, 0};
}

#endif // WEB_SERVER_TLS_STARTUP_H
