#include "web_auth.h"
#include "crypto/hmac_sha256.h"

static std::string hex8(uint32_t v) {
  static const char *H = "0123456789abcdef";
  std::string s(8, '0');
  for(int i = 7; i >= 0; i--) { s[i] = H[v & 0xf]; v >>= 4; }
  return s;
}
static const std::string VER = "v1.";
static const size_t SIG_LEN = 32; // 128-bit truncated hex

std::string session_token_mint(const std::string &secret, uint32_t exp) {
  std::string payload = VER + hex8(exp);
  std::string sig = hmac_sha256_hex(secret, payload).substr(0, SIG_LEN);
  return payload + "." + sig;
}

bool auth_constant_time_equals(const std::string &a, const std::string &b) {
  size_t diff = a.size() ^ b.size();
  size_t n = a.size() < b.size() ? a.size() : b.size();
  for(size_t i = 0; i < n; i++) diff |= (unsigned char)(a[i] ^ b[i]);
  return diff == 0;
}

std::string cookie_extract(const std::string &hdr, const std::string &name) {
  size_t i = 0;
  while(i < hdr.size()) {
    while(i < hdr.size() && (hdr[i] == ' ' || hdr[i] == ';')) i++;
    size_t eq = hdr.find('=', i);
    if(eq == std::string::npos) break;
    std::string key = hdr.substr(i, eq - i);
    size_t end = hdr.find(';', eq + 1);
    std::string val = hdr.substr(eq + 1, (end == std::string::npos ? hdr.size() : end) - (eq + 1));
    if(key == name) return val;
    if(end == std::string::npos) break;
    i = end + 1;
  }
  return "";
}

bool session_token_verify(const std::string &secret, const std::string &token, uint32_t now) {
  // expect "v1." + 8 hex + "." + 32 hex  => length 3 + 8 + 1 + 32 = 44
  if(token.size() != VER.size() + 8 + 1 + SIG_LEN) return false;
  if(token.compare(0, VER.size(), VER) != 0) return false;
  std::string exphex = token.substr(VER.size(), 8);
  if(token[VER.size() + 8] != '.') return false;
  std::string sig = token.substr(VER.size() + 9);
  // parse exp
  uint32_t exp = 0;
  for(char c : exphex) {
    exp <<= 4;
    if(c >= '0' && c <= '9') exp |= (c - '0');
    else if(c >= 'a' && c <= 'f') exp |= (c - 'a' + 10);
    else return false;
  }
  std::string payload = VER + exphex;
  std::string expected = hmac_sha256_hex(secret, payload).substr(0, SIG_LEN);
  if(!auth_constant_time_equals(sig, expected)) return false;
  return now < exp;
}
