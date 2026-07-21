#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "web_auth.h"

static const std::string SECRET = "0011223344556677889900aabbccddee";

TEST_CASE("mint then verify round-trips before expiry") {
  std::string t = session_token_mint(SECRET, 2000);
  CHECK(session_token_verify(SECRET, t, 1000) == true);
}
TEST_CASE("expired token is rejected") {
  std::string t = session_token_mint(SECRET, 2000);
  CHECK(session_token_verify(SECRET, t, 2001) == false);
}
TEST_CASE("tampered signature is rejected") {
  std::string t = session_token_mint(SECRET, 2000);
  t[t.size()-1] = (t[t.size()-1] == 'a') ? 'b' : 'a';
  CHECK(session_token_verify(SECRET, t, 1000) == false);
}
TEST_CASE("wrong secret is rejected") {
  std::string t = session_token_mint(SECRET, 2000);
  CHECK(session_token_verify("deadbeef", t, 1000) == false);
}
TEST_CASE("wrong version prefix is rejected") {
  std::string t = session_token_mint(SECRET, 2000);
  t[1] = '9'; // v9...
  CHECK(session_token_verify(SECRET, t, 1000) == false);
}
TEST_CASE("garbage is rejected, not crashed") {
  CHECK(session_token_verify(SECRET, "", 1000) == false);
  CHECK(session_token_verify(SECRET, "v1.zzzz", 1000) == false);
}
