#include "crash_report_id.h"

#include <stdio.h>
#include <string.h>

#include "crypto/sha256.h"

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

CrashIdentityStore crash_identity_store_decide(const char *have, const char *rid,
                                               const char *key)
{
  if(!crash_reporter_id_valid(rid) || !crash_delete_key_valid(key)) {
    return CrashIdentity_Invalid;
  }
  char haveRid[33], haveKey[65];
  if(!crash_identity_parse(have, haveRid, haveKey)) {
    return CrashIdentity_Write;
  }
  return (0 == strcmp(haveRid, rid) && 0 == strcmp(haveKey, key))
    ? CrashIdentity_Same : CrashIdentity_Conflict;
}

bool crash_gui_request(const char *xRequestedWith)
{
  return NULL != xRequestedWith && 0 == strcmp(xRequestedWith, "OpenEVSE");
}
