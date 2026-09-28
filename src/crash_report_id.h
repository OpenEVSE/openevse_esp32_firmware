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

#endif // CRASH_REPORT_ID_H
