#include "crash_report_id.h"

#include <stdio.h>
#include <string.h>

#include "crypto/sha256.h"

// 8-4-4-4-12
static const int DASH_AT[] = { 8, 13, 18, 23 };

bool crash_report_id_valid(const char *id)
{
  if(!id || 36 != strlen(id)) {
    return false;
  }
  for(int i = 0; i < 36; i++) {
    bool dash = false;
    for(size_t d = 0; d < sizeof(DASH_AT) / sizeof(DASH_AT[0]); d++) {
      if(i == DASH_AT[d]) {
        dash = true;
        break;
      }
    }
    if(dash) {
      if('-' != id[i]) {
        return false;
      }
    } else if(!((id[i] >= '0' && id[i] <= '9') ||
                (id[i] >= 'a' && id[i] <= 'f'))) {
      // Lower case only. Accepting upper case would buy nothing and widen the
      // set of strings that reach a request line.
      return false;
    }
  }
  return true;
}

void crash_defer_token(char *out, uint64_t id, size_t len)
{
  snprintf(out, CRASH_DEFER_TOKEN_LEN, "%08lx%08lx:%lu",
           (unsigned long)(id >> 32), (unsigned long)(id & 0xffffffffUL),
           (unsigned long)len);
}

bool crash_defer_token_matches(const char *stored, uint64_t id, size_t len)
{
  if(!stored) {
    return false;
  }
  char want[CRASH_DEFER_TOKEN_LEN];
  crash_defer_token(want, id, len);
  // Exact, whole-string: a legacy "1" or a truncated token consents to nothing.
  return 0 == strcmp(stored, want);
}

// A 9-character prefix is what the firmware reports; 8 is the floor the broker
// also uses for identifying a build.
#define CRASH_MIN_SHA_PREFIX 8

bool crash_dump_from_running_build(const char *dumpSha, const char *runningSha)
{
  if(!dumpSha || !runningSha) {
    return false;
  }
  size_t n = strlen(dumpSha);
  if(n < CRASH_MIN_SHA_PREFIX || strlen(runningSha) < n) {
    return false;
  }
  return 0 == strncmp(dumpSha, runningSha, n);
}

size_t crash_declared_raw_bytes(size_t imageLen)
{
  return CRASH_UPLOAD_RAW_DUMP ? imageLen : 0;
}

void crash_hex(const uint8_t *in, size_t len, char *out)
{
  static const char digits[] = "0123456789abcdef";
  for(size_t i = 0; i < len; i++) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 0x0f];
  }
  out[2 * len] = '\0';
}

static bool crash_lower_hex(const char *s, size_t want)
{
  if(NULL == s) {
    return false;
  }
  size_t n = 0;
  for(; s[n]; n++) {
    if(n >= want || !((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f'))) {
      return false;
    }
  }
  return n == want;
}

bool crash_reporter_id_valid(const char *id)
{
  return crash_lower_hex(id, CRASH_REPORTER_ID_HEX);
}

bool crash_delete_key_valid(const char *key)
{
  return crash_lower_hex(key, CRASH_DELETE_KEY_HEX);
}

void crash_delete_key_hash(const char *key, char out[65])
{
  uint8_t digest[32];
  sha256((const uint8_t *)key, strlen(key), digest);
  crash_hex(digest, sizeof(digest), out);
}

void crash_identity_format(const char *rid, const char *key, char *out, size_t len)
{
  snprintf(out, len, "%s\n%s\n", rid, key);
}

bool crash_identity_parse(const char *text, char rid[33], char key[65])
{
  if(NULL == text || strlen(text) != CRASH_IDENTITY_LEN - 1 ||
     '\n' != text[CRASH_REPORTER_ID_HEX] ||
     '\n' != text[CRASH_IDENTITY_LEN - 2]) {
    return false;
  }
  memcpy(rid, text, CRASH_REPORTER_ID_HEX);
  rid[CRASH_REPORTER_ID_HEX] = '\0';
  memcpy(key, text + CRASH_REPORTER_ID_HEX + 1, CRASH_DELETE_KEY_HEX);
  key[CRASH_DELETE_KEY_HEX] = '\0';
  return crash_reporter_id_valid(rid) && crash_delete_key_valid(key);
}

CrashForgetDecision crash_forget_decide(bool running, bool hasIdentity, bool netUp,
                                        uint32_t largestBlock, uint32_t minLargest)
{
  if(!hasIdentity) {
    return CrashForget_Nothing;
  }
  if(running) {
    return CrashForget_Busy;
  }
  if(!netUp || largestBlock < minLargest) {
    return CrashForget_Defer;
  }
  return CrashForget_Start;
}
