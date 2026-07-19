#include "ota_url_allow.h"

#include <string>
#include <cctype>

static std::string to_lower(std::string s)
{
  for(char &c : s) {
    c = (char)std::tolower((unsigned char)c);
  }
  return s;
}

// True if `host` equals `suffix` or ends with "." + `suffix`. `host` must
// already be lower-cased; `suffix` is a lower-case literal.
static bool host_matches(const std::string &host, const char *suffix)
{
  std::string suf = suffix;
  if(host == suf) {
    return true;
  }
  std::string dotted = "." + suf;
  return host.size() > dotted.size() &&
         host.compare(host.size() - dotted.size(), dotted.size(), dotted) == 0;
}

bool ota_url_host_allowed(const char *url_c)
{
  if(!url_c) {
    return false;
  }
  std::string url = url_c;

  // Scheme must be https (case-insensitive). Require the "://" separator so a
  // bare host without a scheme is rejected.
  std::string::size_type sep = url.find("://");
  if(sep == std::string::npos) {
    return false;
  }
  if(to_lower(url.substr(0, sep)) != "https") {
    return false;
  }

  std::string::size_type start = sep + 3;

  // The authority ends at the first '/' or '?' — NOT ':', because a ':' can
  // appear inside userinfo (user:pass@host) before the '@'.
  std::string::size_type end = url.size();
  for(std::string::size_type i = start; i < url.size(); i++) {
    char c = url[i];
    if(c == '/' || c == '?') {
      end = i;
      break;
    }
  }
  std::string authority = url.substr(start, end - start);

  // Userinfo is everything up to and including the last '@'.
  std::string::size_type at = authority.rfind('@');
  std::string host = (at == std::string::npos) ? authority : authority.substr(at + 1);

  // Strip the port (first ':' of the real host).
  std::string::size_type colon = host.find(':');
  if(colon != std::string::npos) {
    host = host.substr(0, colon);
  }

  if(host.empty()) {
    return false;
  }
  host = to_lower(host);

  // Allowlist. Edit here to permit other trusted firmware hosts.
  return host_matches(host, "github.com") ||
         host_matches(host, "githubusercontent.com");
}
