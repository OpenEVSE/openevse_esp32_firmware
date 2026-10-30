#include "crash_report.h"

#if ENABLE_CRASH_UPLOAD

#include <LittleFS.h>
#include <string.h>

#include "crash_report_id.h"

#define CRASH_IDENTITY_FILE "/crash_reporter"

bool crash_identity_load(char rid[33], char key[65])
{
  if(!LittleFS.exists(CRASH_IDENTITY_FILE)) {
    return false;
  }
  File f = LittleFS.open(CRASH_IDENTITY_FILE, "r");
  if(!f) {
    return false;
  }
  String text = f.readString();
  f.close();
  return crash_identity_parse(text.c_str(), rid, key);
}

CrashIdentityStore crash_identity_store(const char *rid, const char *key)
{
  String have;
  if(LittleFS.exists(CRASH_IDENTITY_FILE)) {
    File f = LittleFS.open(CRASH_IDENTITY_FILE, "r");
    if(f) {
      have = f.readString();
      f.close();
    }
  }
  CrashIdentityStore decision =
    crash_identity_store_decide(have.length() ? have.c_str() : nullptr, rid, key);
  if(CrashIdentity_Write != decision) {
    return decision;
  }
  char text[CRASH_IDENTITY_LEN];
  crash_identity_format(rid, key, text, sizeof(text));
  File f = LittleFS.open(CRASH_IDENTITY_FILE, "w");
  if(!f) {
    return CrashIdentity_WriteFailed;
  }
  size_t n = f.print(text);
  f.close();
  return n == strlen(text) ? CrashIdentity_Write : CrashIdentity_WriteFailed;
}

bool crash_identity_forget()
{
  if(LittleFS.exists(CRASH_IDENTITY_FILE)) {
    LittleFS.remove(CRASH_IDENTITY_FILE);
  }
  return !LittleFS.exists(CRASH_IDENTITY_FILE);
}

#endif // ENABLE_CRASH_UPLOAD
