#include "doctest.h"
#include "web_auth.h"

TEST_CASE("constant-time equals") {
  CHECK(auth_constant_time_equals("abc", "abc"));
  CHECK_FALSE(auth_constant_time_equals("abc", "abd"));
  CHECK_FALSE(auth_constant_time_equals("abc", "ab"));
  CHECK(auth_constant_time_equals("", ""));
}
TEST_CASE("cookie_extract finds the named value") {
  CHECK(cookie_extract("a=1; oevse_session=xyz; b=2", "oevse_session") == "xyz");
  CHECK(cookie_extract("oevse_session=xyz", "oevse_session") == "xyz");
  CHECK(cookie_extract("a=1; b=2", "oevse_session") == "");
  CHECK(cookie_extract("", "oevse_session") == "");
  // must not match a suffix/substring key
  CHECK(cookie_extract("not_oevse_session=nope", "oevse_session") == "");
}
