#include "ha_oauth.h"
#include <ArduinoJson.h>

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

std::string ha_build_redirect_uri(const std::string &base_url) {
  return base_url + "/ha_callback";
}

std::string ha_build_authorize_url(const std::string &ha_url,
                                   const std::string &client_id,
                                   const std::string &redirect_uri,
                                   const std::string &state) {
  std::string base = ha_url;
  while (!base.empty() && base.back() == '/') base.pop_back();
  std::string url = base + "/auth/authorize";
  url += "?response_type=code";
  url += "&client_id=" + ha_url_encode(client_id);
  url += "&redirect_uri=" + ha_url_encode(redirect_uri);
  url += "&state=" + ha_url_encode(state);
  return url;
}

std::string ha_build_token_exchange_body(const std::string &client_id,
                                         const std::string &code) {
  std::string b = "grant_type=authorization_code";
  b += "&code=" + ha_url_encode(code);
  b += "&client_id=" + ha_url_encode(client_id);
  return b;
}

std::string ha_build_refresh_body(const std::string &client_id,
                                  const std::string &refresh_token) {
  std::string b = "grant_type=refresh_token";
  b += "&refresh_token=" + ha_url_encode(refresh_token);
  b += "&client_id=" + ha_url_encode(client_id);
  return b;
}

bool ha_parse_token_response(const std::string &json, HaTokens &out) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    return false;
  }
  if (!doc["access_token"].is<const char *>()) {
    return false;
  }
  out.access_token = doc["access_token"].as<const char *>();
  if (doc["refresh_token"].is<const char *>()) {
    out.refresh_token = doc["refresh_token"].as<const char *>();
  } else {
    out.refresh_token.clear();
  }
  out.expires_in = doc["expires_in"] | 0L;
  return true;
}

uint64_t ha_compute_expiry(uint64_t now_unix, long expires_in) {
  if (expires_in <= 0) return 0;
  return now_unix + (uint64_t)expires_in;
}

bool ha_refresh_due(uint64_t expiry_unix, uint64_t now_unix, uint64_t margin_sec) {
  if (expiry_unix == 0) return true; // unknown expiry -> refresh
  uint64_t threshold = (expiry_unix > margin_sec) ? (expiry_unix - margin_sec) : 0;
  return now_unix >= threshold;
}

bool ha_parse_entity_state(const std::string &json, std::string &out) {
  // Filter so only "state" is materialized — entity `attributes` can be large.
  StaticJsonDocument<32> filter;
  filter["state"] = true;
  // 384 bytes covers the HA-spec max 255-char state value plus object overhead on
  // both 32-bit (ESP32) and 64-bit (native test). The filter keeps `attributes` out.
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) {
    return false;
  }
  if (!doc["state"].is<const char *>()) {
    return false;
  }
  std::string s = doc["state"].as<const char *>();
  if (s.empty() || s == "unknown" || s == "unavailable") {
    return false;
  }
  out = s;
  return true;
}
