#include "ha_oauth.h"

std::string ha_url_encode(const std::string &in) {
  static const char *hex = "0123456789ABCDEF";
  auto is_unreserved = [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~';
  };
  std::string out;
  out.reserve(in.size() * 3);
  for (unsigned char c : in) {
    if (is_unreserved(c)) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

std::string ha_derive_base_url(const std::string &scheme, const std::string &host) {
  std::string s = scheme.empty() ? "http" : scheme;
  std::string h = host;
  // strip any trailing slash on host (defensive)
  while (!h.empty() && h.back() == '/') h.pop_back();
  return s + "://" + h;
}
