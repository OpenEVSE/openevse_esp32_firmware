#include "doctest.h"
#include "web_auth.h"

TEST_CASE("throttle: trips at the lock threshold") {
  AuthThrottle t;
  for(int i = 0; i < AUTH_THROTTLE_LOCK_AT - 1; i++) {
    auth_throttle_record_failure(t, 100);
  }
  CHECK_FALSE(auth_throttle_locked(t, 100));
  auth_throttle_record_failure(t, 100);
  CHECK(auth_throttle_locked(t, 100));
}

TEST_CASE("throttle: a correct credential (success) clears it immediately") {
  AuthThrottle t;
  for(int i = 0; i < 20; i++) auth_throttle_record_failure(t, 100);
  CHECK(auth_throttle_locked(t, 100));
  auth_throttle_record_success(t, 200);
  CHECK(auth_throttle_effective(t, 200) == 0);
  CHECK_FALSE(auth_throttle_locked(t, 200));
}

TEST_CASE("throttle: decays one failure per decay period") {
  AuthThrottle t;
  for(int i = 0; i < 10; i++) auth_throttle_record_failure(t, 0);
  CHECK(auth_throttle_effective(t, 0) == 10);
  CHECK(auth_throttle_effective(t, 3 * AUTH_THROTTLE_DECAY_SECS) == 7);
  // fully cools off — no permanent lock, so a re-armable DoS can't persist
  CHECK(auth_throttle_effective(t, 10 * AUTH_THROTTLE_DECAY_SECS) == 0);
}

TEST_CASE("throttle: a fresh failure rebases onto the decayed count") {
  AuthThrottle t;
  for(int i = 0; i < 10; i++) auth_throttle_record_failure(t, 0);
  // after 8 periods effective is 2; one more failure -> 3, not 11
  auth_throttle_record_failure(t, 8 * AUTH_THROTTLE_DECAY_SECS);
  CHECK(auth_throttle_effective(t, 8 * AUTH_THROTTLE_DECAY_SECS) == 3);
}

TEST_CASE("throttle: time going backwards never underflows") {
  AuthThrottle t;
  auth_throttle_record_failure(t, 500);
  CHECK(auth_throttle_effective(t, 400) == 1);
}
