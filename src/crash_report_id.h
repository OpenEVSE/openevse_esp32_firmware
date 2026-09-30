#ifndef CRASH_REPORT_ID_H
#define CRASH_REPORT_ID_H

#include <stddef.h>
#include <stdint.h>

// True if the dump's ELF hash (the 9-character prefix /debug/crash reports)
// names the firmware that is running now. After an OTA it does not, and the
// running version string would file the crash under a build that did not
// crash. Anything too short to identify a build answers false.
bool crash_dump_from_running_build(const char *dumpSha, const char *runningSha);

// The reporter identity (sent in place of any hardware id). The chip id is
// MAC-derived -- personal data under GDPR -- and only ~24 bits of it are
// unknown within Espressif's OUIs, so even a hash of it brute-forces back in
// seconds. Instead the browser generates, before this charger's first report,
// and stores on the charger (POST /debug/crash/identity):
//   - a random reporter id (16 bytes, 32 hex), sent with every report, and
//   - a random delete key (32 bytes, 64 hex) that is never sent with a report.
//     Reports carry only its SHA-256, and presenting the key erases them.
// The browser generates them because its crypto.getRandomValues() is a
// CSPRNG everywhere, while the ESP32's RNG is only truly random with the RF
// running -- and an Ethernet charger may have Wi-Fi off.
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
