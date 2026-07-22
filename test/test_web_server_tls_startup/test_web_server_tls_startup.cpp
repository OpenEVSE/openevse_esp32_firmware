#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "web_server_tls_startup.h"

class FakeHttpsListener
{
  public:
    bool start_succeeds = true;
    unsigned int calls = 0;

    bool begin(const char *, const char *)
    {
      ++calls;
      return start_succeeds;
    }
};

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
                                   [&listener](const char *cert, const char *key) {
                                     return listener.begin(cert, key);
                                   }) == expected);
      CHECK(listener.calls == (expected ? 1U : 0U));
    }
  }
}

TEST_CASE("HTTPS listener failure keeps HTTPS inactive")
{
  FakeHttpsListener listener;
  listener.start_succeeds = false;

  CHECK_FALSE(web_server_start_https("certificate", "private key",
                                     [&listener](const char *cert, const char *key) {
                                       return listener.begin(cert, key);
                                     }));
  CHECK(listener.calls == 1);
}
