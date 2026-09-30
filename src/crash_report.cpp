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

bool crash_identity_store(const char *rid, const char *key)
{
  if(!crash_reporter_id_valid(rid) || !crash_delete_key_valid(key)) {
    return false;
  }
  char haveRid[33], haveKey[65];
  if(crash_identity_load(haveRid, haveKey)) {
    return 0 == strcmp(haveRid, rid) && 0 == strcmp(haveKey, key);
  }
  char text[CRASH_IDENTITY_LEN];
  crash_identity_format(rid, key, text, sizeof(text));
  File f = LittleFS.open(CRASH_IDENTITY_FILE, "w");
  if(!f) {
    return false;
  }
  size_t n = f.print(text);
  f.close();
  return n == strlen(text);
}

bool crash_identity_forget()
{
  if(LittleFS.exists(CRASH_IDENTITY_FILE)) {
    LittleFS.remove(CRASH_IDENTITY_FILE);
  }
  return !LittleFS.exists(CRASH_IDENTITY_FILE);
}

#endif // ENABLE_CRASH_UPLOAD
