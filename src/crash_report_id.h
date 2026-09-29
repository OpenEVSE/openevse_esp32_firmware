#ifndef CRASH_REPORT_ID_H
#define CRASH_REPORT_ID_H

// True if `id` is exactly a lower-case hyphenated UUID, and so may be built
// into a request path.
//
// The broker's reply is attacker-controlled the moment the broker is, and this
// id is the only part of it the device acts on. Rather than sanitising the
// path the broker suggests, the device validates the id and builds the path
// itself -- there is then no string from the network in the request line at
// all.
bool crash_report_id_valid(const char *id);

#include <stddef.h>
#include <stdint.h>

// Identity of the dump a deferred click consented to (spec section 8).
//
// The deferred-upload flag stores this token, not just "armed". At the next
// boot the token is compared with the dump that is there: if the charger
// crashed again after the click, that newer dump was never offered, and it is
// not sent. The first 64 bits of the image's SHA-256, plus its length.
//
// NOT a CRC32. An ESP-IDF core dump ends with a CRC32 of itself, and the CRC32
// of any message followed by its own CRC is a constant -- so every valid dump
// has the same whole-image CRC32, and a CRC-based identity would match any
// dump at all. Measured on the bench: two different dumps, CRC32 2144df1c both.
#define CRASH_DEFER_TOKEN_LEN 32   // 16 hex + ':' + up to 10 digits + NUL
void crash_defer_token(char *out, uint64_t id, size_t len);
bool crash_defer_token_matches(const char *stored, uint64_t id, size_t len);

// True if the dump's ELF hash (the 9-character prefix /debug/crash reports)
// names the firmware that is running now. After an OTA it does not, and the
// running version string would file the crash under a build that did not
// crash. Anything too short to identify a build answers false.
bool crash_dump_from_running_build(const char *dumpSha, const char *runningSha);

// How many bytes of the raw image the metadata step declares, and so PUTs
// afterwards. 0 unless the build sets CRASH_UPLOAD_RAW_DUMP: the image is a copy
// of RAM, which can hold Wi-Fi and MQTT credentials that no redaction reaches,
// and the decoded summary alone is enough for the broker to name the frames.
// Kept as a build option for a developer chasing a crash on their own unit.
#ifndef CRASH_UPLOAD_RAW_DUMP
#define CRASH_UPLOAD_RAW_DUMP 0
#endif
size_t crash_declared_raw_bytes(size_t imageLen);

// The reporter identity (sent in place of any hardware id). The chip id is
// MAC-derived -- personal data under GDPR -- and only ~24 bits of it are
// unknown within Espressif's OUIs, so even a hash of it brute-forces back in
// seconds. Instead the device generates, at its first upload:
//   - a random reporter id (16 bytes, 32 hex), sent with every report, and
//   - a random delete key (32 bytes, 64 hex) that is never sent with a report.
//     Reports carry only its SHA-256, and presenting the key erases them.
#define CRASH_REPORTER_ID_HEX 32
#define CRASH_DELETE_KEY_HEX  64
// rid + '\n' + key + '\n' + NUL
#define CRASH_IDENTITY_LEN    (CRASH_REPORTER_ID_HEX + CRASH_DELETE_KEY_HEX + 3)

void crash_hex(const uint8_t *in, size_t len, char *out);   // out: 2*len + 1
bool crash_reporter_id_valid(const char *id);
bool crash_delete_key_valid(const char *key);
// SHA-256 of the key's hex text, as hex. Matches what the broker computes from
// the key presented to it.
void crash_delete_key_hash(const char *key, char out[65]);
void crash_identity_format(const char *rid, const char *key, char *out, size_t len);
bool crash_identity_parse(const char *text, char rid[33], char key[65]);

#endif // CRASH_REPORT_ID_H
