#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

// Crash reporting, charger side (spec section 3, as reworked after review on
// #1306). The browser does the sending: it reads the report from
// GET /debug/crash/report, POSTs it to the broker itself, and erases the dump
// once the broker has it. The charger needs no internet access, holds no TLS
// client for this, and only keeps the one thing that has to outlive a browser
// tab -- the reporter identity -- so "Delete my reports" works from anywhere.

#if ENABLE_CRASH_UPLOAD

#include "crash_report_id.h"

// Where the browser sends reports. Given to the GUI rather than built into it,
// so a bench build can point at a test broker with one -D flag.
#ifndef CRASH_BROKER_URL
#define CRASH_BROKER_URL "https://crash.openevse.com"
#endif

// The stored reporter identity (crash_report_id.h), if the browser has set one.
bool crash_identity_load(char rid[33], char key[65]);

// Store an identity the browser generated (crash_identity_store_decide says
// what may happen). Write and Same leave `rid`/`key` stored.
CrashIdentityStore crash_identity_store(const char *rid, const char *key);

// Forget the identity, after the browser has had its reports erased. True if
// none is stored afterwards.
bool crash_identity_forget();

#endif // ENABLE_CRASH_UPLOAD

#endif // CRASH_REPORT_H
