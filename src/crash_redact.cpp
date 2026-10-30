#include "crash_redact.h"

#include <string.h>

// What a maintainer needs to reproduce a crash: which subsystems were running,
// and what build it was. Nothing here is a secret, and nothing here is a free
// text field a user could have pasted a secret into.
static const char *ALLOWED[] = {
  "buildenv",
  "version",
  "current_shaper_enabled",
  "divert_enabled",
  "emoncms_enabled",
  "loadsharing_enabled",
  "mqtt_enabled",
  "ocpp_enabled",
  "rfid_enabled",
  "sntp_enabled",
  "temp_throttle_enabled",
  "tesla_enabled",
};

bool crash_redact_key_allowed(const char *key)
{
  if(!key || '\0' == key[0]) {
    return false;
  }
  for(size_t i = 0; i < sizeof(ALLOWED) / sizeof(ALLOWED[0]); i++) {
    if(0 == strcmp(key, ALLOWED[i])) {
      return true;
    }
  }
  return false;
}

void crash_redact_config(JsonObjectConst full, JsonObject out)
{
  for(JsonPairConst kv : full) {
    if(crash_redact_key_allowed(kv.key().c_str())) {
      out[kv.key()] = kv.value();
    }
  }
}
