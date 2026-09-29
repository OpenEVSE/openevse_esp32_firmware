#ifndef OEVSE_WEB_AUTH_H
#define OEVSE_WEB_AUTH_H
#include <string>
#include <cstdint>

std::string session_token_mint(const std::string &secret, uint32_t exp);
bool session_token_verify(const std::string &secret, const std::string &token, uint32_t now);
bool auth_constant_time_equals(const std::string &a, const std::string &b);
std::string cookie_extract(const std::string &cookie_header, const std::string &name);

// -------------------------------------------------------------------
// Decaying failed-authentication throttle
//
// Counts failed credential attempts (both HTTP Basic and POST /login) and
// "cools off" over time, so a burst of bad guesses trips a soft lock that
// clears itself.  Crucially it is only ever consulted to reject *failed*
// attempts: a request bearing correct credentials always authenticates and
// resets the counter, so a flood of bad logins can never lock out a machine
// client (Home Assistant, MQTT) that holds the right password.  `now` is a
// monotonic seconds value supplied by the caller (millis()/1000 on device),
// deliberately independent of the wall clock so an SNTP jump can't distort it.
// -------------------------------------------------------------------
constexpr uint32_t AUTH_THROTTLE_DECAY_SECS = 60;   // one failure forgiven per minute
constexpr uint16_t AUTH_THROTTLE_LOCK_AT    = 10;   // "hot" once this many recent fails

struct AuthThrottle {
  uint16_t fails   = 0;
  uint32_t updated = 0;
};

// Failures remaining after applying time decay (never underflows).
uint16_t auth_throttle_effective(const AuthThrottle &t, uint32_t now);
// Rebase to the decayed count, then add one failure and stamp `now`.
void     auth_throttle_record_failure(AuthThrottle &t, uint32_t now);
// A correct credential clears the counter outright.
void     auth_throttle_record_success(AuthThrottle &t, uint32_t now);
// True once the decayed failure count reaches the lock threshold.
bool     auth_throttle_locked(const AuthThrottle &t, uint32_t now);
#endif
