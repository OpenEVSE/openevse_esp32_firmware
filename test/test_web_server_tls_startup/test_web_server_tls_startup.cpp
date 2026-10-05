#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "web_server_tls_startup.h"

/** Count TLS startup attempts and return a configurable success result. */
class FakeHttpsListener
{
  public:
    bool start_succeeds = true;
    unsigned int calls = 0;

    /** Record an attempted listener start without opening a socket. */
    bool begin(const char *, const char *)
    {
      ++calls;
      return start_succeeds;
    }
};

/** Missing or empty material must prevent the TLS callback from running. */
TEST_CASE("HTTPS requires non-empty certificate and private key")
{
  for(const char *certificate : {static_cast<const char *>(nullptr), "", "certificate"})
  {
    for(const char *private_key : {static_cast<const char *>(nullptr), "", "private key"})
    {
      CAPTURE(certificate);
      CAPTURE(private_key);
      FakeHttpsListener listener;
      bool expected = nullptr != certificate && '\0' != certificate[0] &&
                      nullptr != private_key && '\0' != private_key[0];

      CHECK(web_server_start_https(certificate, private_key,
                                   /** Count a TLS attempt for this material combination. */
                                   [&listener](const char *cert, const char *key) {
                                     return listener.begin(cert, key);
                                   }) == expected);
      CHECK(listener.calls == (expected ? 1U : 0U));
    }
  }
}

/** A rejected TLS startup must not be reported as an active HTTPS listener. */
TEST_CASE("HTTPS listener failure keeps HTTPS inactive")
{
  FakeHttpsListener listener;
  listener.start_succeeds = false;

  CHECK_FALSE(web_server_start_https("certificate", "private key",
                                     /** Return the fake listener's rejected startup. */
                                     [&listener](const char *cert, const char *key) {
                                       return listener.begin(cert, key);
                                     }));
  CHECK(listener.calls == 1);
}

/** Both failed attempts must yield inactive state and no advertised port. */
TEST_CASE("failed HTTPS and HTTP listeners leave the web server inactive")
{
  unsigned int https_calls = 0;
  unsigned int http_calls = 0;

  WebServerListenerState state = web_server_start_listeners(
    "certificate", "private key", 443, 80,
    /** Count and reject the TLS attempt. */
    [&https_calls](const char *, const char *) {
      ++https_calls;
      return false;
    },
    /** Count and reject the HTTP fallback attempt. */
    [&http_calls]() {
      ++http_calls;
      return false;
    });

  CHECK(https_calls == 1);
  CHECK(http_calls == 1);
  CHECK_FALSE(state.started);
  CHECK_FALSE(state.https);
  CHECK(state.port == 0);
}

/** Successful TLS selects its configured port without invoking the fallback. */
TEST_CASE("successful HTTPS selects its port without starting an HTTP fallback")
{
  unsigned int http_calls = 0;
  WebServerListenerState state = web_server_start_listeners(
    "certificate", "private key", 8443, 8080,
    /** Model a successful TLS bind. */
    [](const char *, const char *) { return true; },
    /** Count any unexpected HTTP fallback after successful TLS. */
    [&http_calls]() {
      ++http_calls;
      return true;
    });

  CHECK(state.started);
  CHECK(state.https);
  CHECK(state.port == 8443);
  CHECK(http_calls == 0);
}

/** Missing material and failed TLS must both select the working HTTP listener. */
TEST_CASE("HTTP fallback selects its port after missing material or failed TLS")
{
  for(const char *certificate : {static_cast<const char *>(nullptr), "", "certificate"})
  {
    unsigned int https_calls = 0;
    unsigned int http_calls = 0;
    WebServerListenerState state = web_server_start_listeners(
      certificate, "private key", 8443, 8080,
      /** Count and reject TLS when the supplied material permits an attempt. */
      [&https_calls](const char *, const char *) {
        ++https_calls;
        return false;
      },
      /** Count and accept the HTTP fallback. */
      [&http_calls]() {
        ++http_calls;
        return true;
      });

    CHECK(state.started);
    CHECK_FALSE(state.https);
    CHECK(state.port == 8080);
    CHECK(https_calls == (certificate != nullptr && certificate[0] != '\0' ? 1U : 0U));
    CHECK(http_calls == 1);
  }
}
