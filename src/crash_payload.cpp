#include "crash_payload.h"

#if ENABLE_CRASH_UPLOAD

#include <esp_app_desc.h>

#include "emonesp.h"          // buildenv, currentfirmware, serial
#include "app_config.h"
#include "diagnostics.h"
#include "crash_redact.h"
#include "crash_report_id.h"
#include "input.h"             // evse
#include <espal.h>

void crash_payload_build(JsonDocument &doc, size_t rawBytes,
                         const char *reporterId, const char *deleteKeyHash)
{
  doc["buildenv"] = buildenv;

  // No hardware id. The chip id is MAC-derived, and a hash of it brute-forces
  // back; a random reporter id groups this charger's reports without naming
  // it (crash_report_id.h).
  doc["reporter_id"] = reporterId;
  doc["delete_key_hash"] = deleteKeyHash;
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

    // The version is the running firmware's, which is only the crashed
    // build's if no update happened in between. After an OTA it would file
    // the crash under a build that did not crash -- so say "unknown" then and
    // keep the running one separately. The ELF lookup is unaffected either
    // way: it keys on the dump's own hash.
    char running[17] = "";
    esp_app_get_elf_sha256(running, sizeof(running));
    const char *dumpSha = sd["elf_sha256"] | "";
    if(crash_dump_from_running_build(dumpSha, running)) {
      doc["version"] = currentfirmware;
    } else {
      doc["version"] = "unknown";
      doc["running_version"] = currentfirmware;
    }
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

#endif // ENABLE_CRASH_UPLOAD
