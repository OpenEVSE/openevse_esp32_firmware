#include "crash_report_id.h"

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
