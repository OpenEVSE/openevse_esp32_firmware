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
// not sent. "crc32:length" of the whole image -- two dumps agreeing on both is
// not a case worth designing for.
#define CRASH_DEFER_TOKEN_LEN 20   // "xxxxxxxx:" + up to 10 digits + NUL
void crash_defer_token(char *out, uint32_t crc, size_t len);
bool crash_defer_token_matches(const char *stored, uint32_t crc, size_t len);

// True if the dump's ELF hash (the 9-character prefix /debug/crash reports)
// names the firmware that is running now. After an OTA it does not, and the
// running version string would file the crash under a build that did not
// crash. Anything too short to identify a build answers false.
bool crash_dump_from_running_build(const char *dumpSha, const char *runningSha);

#endif // CRASH_REPORT_ID_H
