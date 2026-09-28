#ifndef OTA_URL_ALLOW_H
#define OTA_URL_ALLOW_H

#include "crash_host.h"

// Pure, framework-free allowlist check for OTA firmware-fetch URLs.
//
// Returns true only if `url` is an https URL whose host is github.com (or a
// subdomain) or a *.githubusercontent.com host — the origins OpenEVSE release
// downloads are served from (github.com redirects to the githubusercontent
// CDN). Parses per the URL spec: userinfo is everything before the last '@' in
// the authority, and the port follows the host's ':'. This makes
// "https://github.com:x@evil.example/..." resolve to host "evil.example" and be
// rejected, matching how the underlying HTTP client resolves it.
//
// No Arduino/String dependency so it can be unit-tested on the build host.
bool ota_url_host_allowed(const char *url);

// True if `url` is an https URL on the one compiled-in crash-broker host
// (CRASH_BROKER_HOST). Exact match, no subdomains: see crash_host.h for why
// this is not a config value and why the list has exactly one entry.
bool crash_url_host_allowed(const char *url);

#endif // OTA_URL_ALLOW_H
