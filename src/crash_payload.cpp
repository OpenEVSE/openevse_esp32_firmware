#include "crash_payload.h"

#include "emonesp.h"          // buildenv, currentfirmware, serial
#include "app_config.h"
#include "diagnostics.h"
#include "crash_redact.h"
#include "input.h"             // evse
#include <espal.h>

void crash_payload_build(JsonDocument &doc, size_t rawBytes)
{
  doc["version"] = currentfirmware;
  doc["buildenv"] = buildenv;

  // The ESP's own id, not evse.getChipId(). The controller's chip id is what
  // scripts/symbolize_crash.py sent, which collapses every unit with a fake or
  // absent controller onto one value and makes the by-chip index useless.
  doc["chip_id"] = serial;
  doc["espinfo"] = ESPAL.getChipInfo();
  // The controller's own firmware version (spec §5). Absent, not empty, when
  // no controller has answered -- an empty string here is indistinguishable
  // from "an OpenEVSE running version ''".
  const char *evseFw = evse.getFirmwareVersion();   // const char *, not String
  if(evseFw && '\0' != evseFw[0]) {
    doc["firmware"] = evseFw;
  }

  // The decoded summary: panic reason, faulting task, PC, backtrace,
  // elf_sha256. This is what gets symbolized; the raw image is for the
  // questions it cannot answer.
  JsonObject summary = doc.createNestedObject("summary");
  {
    DynamicJsonDocument sd(JSON_OBJECT_SIZE(12) + JSON_ARRAY_SIZE(16) + 640);
    diagnostics_coredump_json(sd);
    for(JsonPair kv : sd.as<JsonObject>()) {
      summary[kv.key()] = kv.value();
    }
    // Hoisted to the top level: it is the ELF lookup key, and the broker
    // reads it there.
    doc["elf_sha256"] = sd["elf_sha256"];
    doc["bt"] = sd["bt"];
  }

  JsonObject diag = doc.createNestedObject("diagnostics");
  {
    DynamicJsonDocument dd(1024);
    diagnostics_status(dd);
    for(JsonPair kv : dd.as<JsonObject>()) {
      diag[kv.key()] = kv.value();
    }
  }

  {
    // hideSecrets = true is belt and braces. crash_redact_config is what
    // actually keeps credentials out, because it names what may leave rather
    // than what may not (spec §5) -- but there is no reason to materialise
    // them in a document at all on the way past.
    DynamicJsonDocument cfg(4096);
    config_serialize(cfg, /*longNames*/ true, /*compactOutput*/ false,
                     /*hideSecrets*/ true);
    JsonObject redacted = doc.createNestedObject("config");
    crash_redact_config(cfg.as<JsonObjectConst>(), redacted);
  }

  if(rawBytes > 0) {
    doc["raw_bytes"] = (uint32_t)rawBytes;
  }
}
