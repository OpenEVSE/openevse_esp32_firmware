#ifndef HA_OAUTH_H
#define HA_OAUTH_H

#include <string>
#include <cstdint>

// Parsed token response from HA's /auth/token endpoint.
struct HaTokens {
  std::string access_token;
  std::string refresh_token;
  long expires_in = 0; // seconds
};

// Percent-encode a string for use in a URL query value.
std::string ha_url_encode(const std::string &in);

// Build "<scheme>://<host>" with no trailing slash.
// host is the raw HTTP Host header value (may include :port).
std::string ha_derive_base_url(const std::string &scheme, const std::string &host);

// base_url + "/ha_callback"
std::string ha_build_redirect_uri(const std::string &base_url);

// HA authorize URL:
//   <ha_url>/auth/authorize?response_type=code&client_id=..&redirect_uri=..&state=..
std::string ha_build_authorize_url(const std::string &ha_url,
                                   const std::string &client_id,
                                   const std::string &redirect_uri,
                                   const std::string &state);

// x-www-form-urlencoded body for grant_type=authorization_code.
std::string ha_build_token_exchange_body(const std::string &client_id,
                                         const std::string &code);

// x-www-form-urlencoded body for grant_type=refresh_token.
std::string ha_build_refresh_body(const std::string &client_id,
                                  const std::string &refresh_token);

// Parse the /auth/token JSON response. Returns true on success
// (access_token and expires_in present); refresh_token may be empty on refresh.
bool ha_parse_token_response(const std::string &json, HaTokens &out);

// Absolute expiry = now_unix + expires_in (0 if expires_in <= 0).
uint64_t ha_compute_expiry(uint64_t now_unix, long expires_in);

// True if the token should be refreshed: now_unix >= expiry_unix - margin_sec.
bool ha_refresh_due(uint64_t expiry_unix, uint64_t now_unix, uint64_t margin_sec);

#endif // HA_OAUTH_H
