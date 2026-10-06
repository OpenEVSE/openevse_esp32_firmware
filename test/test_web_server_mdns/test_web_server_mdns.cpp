#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "web_server_mdns.h"
#include <string>
#include <vector>

/** Record mDNS registrations with an independent result for each service. */
struct FakeMdnsResponder {
  bool http_succeeds;
  bool openevse_succeeds;
  std::vector<std::string> services;

  /** Check the requested transport/port and return the selected service's result. */
  bool addService(const char *service, const char *protocol, uint16_t port) {
    CHECK(std::string(protocol) == "tcp");
    CHECK(port == 8443);
    services.emplace_back(service);
    return std::string(service) == "http" ? http_succeeds : openevse_succeeds;
  }
};

/** Report every failed service and avoid TXT publication for a missing OpenEVSE service. */
TEST_CASE("mDNS publication handles all service registration outcomes")
{
  for(bool http_succeeds : {false, true}) {
    for(bool openevse_succeeds : {false, true}) {
      CAPTURE(http_succeeds);
      CAPTURE(openevse_succeeds);
      FakeMdnsResponder responder{http_succeeds, openevse_succeeds, {}};
      unsigned int metadata_calls = 0;
      std::vector<std::string> failures;

      web_server_publish_mdns(responder, 8443,
        /** Verify metadata follows both registration attempts and count publication. */
        [&]() {
          CHECK(responder.services == std::vector<std::string>{"http", "openevse"});
          ++metadata_calls;
        },
        /** Capture each failed service and verify its diagnostic identifies the port. */
        [&](const char *service, uint16_t port) {
          CHECK(port == 8443);
          failures.emplace_back(service);
        });

      CHECK(responder.services == std::vector<std::string>{"http", "openevse"});
      CHECK(metadata_calls == (openevse_succeeds ? 1U : 0U));
      std::vector<std::string> expected_failures;
      if(!http_succeeds) {
        expected_failures.emplace_back("http");
      }
      if(!openevse_succeeds) {
        expected_failures.emplace_back("openevse");
      }
      CHECK(failures == expected_failures);
    }
  }
}
