#include "crash_report_id.h"

#include <stdio.h>
#include <string.h>

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
