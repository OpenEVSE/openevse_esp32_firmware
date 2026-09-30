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

  // The authority ends at the first '/' ONLY. Mongoose's userinfo scan
  // (mg_parse_uri, P_USER_INFO) terminates on '@', '[' or '/', so ':', '?' and
  // '#' are NOT authority delimiters for the host it actually connects to.
  // Ending on '?' here (as an earlier version did) would let
  // "https://github.com?@evil.example/..." parse to host "github.com" while the
  // client connects to "evil.example".
  std::string::size_type end = url.find('/', start);
  if(end == std::string::npos) {
    end = url.size();
  }
  std::string authority = url.substr(start, end - start);

  // Reject anything that could make our host differ from the client's, rather
  // than trying to re-derive the exact parser. A firmware URL never needs
  // userinfo, IPv6 literals, or stray delimiters in its authority.
  if(authority.find_first_of("@[]?#\\ \t") != std::string::npos) {
    return false;
  }

  // Strip the port.
  std::string host = authority.substr(0, authority.find(':'));

  if(host.empty()) {
    return false;
  }

  // Host must be a plain DNS name (letters, digits, '.', '-').
  for(char c : host) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-';
    if(!ok) {
      return false;
    }
  }
  host = to_lower(host);

  // Allowlist. Edit here to permit other trusted firmware hosts.
  return host_matches(host, "github.com") ||
         host_matches(host, "githubusercontent.com");
}
