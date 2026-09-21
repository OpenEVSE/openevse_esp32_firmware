#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_RFID_USER)
#undef ENABLE_DEBUG
#endif

#include "rfid_user.h"
#include "debug.h"
#include <LittleFS.h>

const char* RfidUser::RFID_USERS_FILE = "/rfid_users.json";
static const char* RFID_USERS_BACKUP_FILE = "/rfid_users.json.bak";

bool RfidUser::load(JsonDocument &doc)
{
  // Recover an interrupted replacement that moved the old file aside but did
  // not commit the temp file. If restoration itself fails, the backup remains
  // readable and is still preferable to silently treating the mapping as new.
  const char* path = RFID_USERS_FILE;
  if(!LittleFS.exists(RFID_USERS_FILE) && LittleFS.exists(RFID_USERS_BACKUP_FILE)) {
    if(!LittleFS.rename(RFID_USERS_BACKUP_FILE, RFID_USERS_FILE)) {
      path = RFID_USERS_BACKUP_FILE;
    }
  }

  File file = LittleFS.open(path, "r");
  if(!file) {
    DBUGLN("RFID users file not found, starting with empty mapping");
    return false;
  }

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if(error) {
    DBUGF("Failed to parse RFID users file: %s", error.c_str());
    return false;
  }

  return true;
}

bool RfidUser::save(const JsonDocument &doc)
{
  // Write-then-rename so a failed serialization or a reset mid-write can
  // never leave /rfid_users.json truncated or empty.
  const char* tempPath = "/rfid_users.json.tmp";

  File file = LittleFS.open(tempPath, "w");
  if(!file) {
    DBUGLN("Failed to open RFID users temp file for writing");
    return false;
  }

  if(serializeJson(doc, file) == 0) {
    DBUGLN("Failed to write RFID users temp file");
    file.close();
    LittleFS.remove(tempPath);
    return false;
  }

  file.close();

  const bool hadExisting = LittleFS.exists(RFID_USERS_FILE);
  if(hadExisting) {
    // LittleFS rename does not provide a portable overwrite guarantee. Keep
    // the old mapping recoverable until the new file has been committed.
    if(LittleFS.exists(RFID_USERS_BACKUP_FILE)) {
      LittleFS.remove(RFID_USERS_BACKUP_FILE);
    }
    if(!LittleFS.rename(RFID_USERS_FILE, RFID_USERS_BACKUP_FILE)) {
      DBUGLN("Failed to back up RFID users file before replacement");
      LittleFS.remove(tempPath);
      return false;
    }
  }

  if(!LittleFS.rename(tempPath, RFID_USERS_FILE)) {
    DBUGLN("Failed to rename RFID users temp file into place");
    if(hadExisting && !LittleFS.rename(RFID_USERS_BACKUP_FILE, RFID_USERS_FILE)) {
      DBUGLN("Failed to restore RFID users backup; backup retained");
    }
    return false;
  }

  if(LittleFS.exists(RFID_USERS_BACKUP_FILE)) {
    LittleFS.remove(RFID_USERS_BACKUP_FILE);
  }

  return true;
}

String RfidUser::getUserName(const String &rfidTag)
{
  if(rfidTag.length() == 0) {
    return "";
  }

  JsonDocument doc;
  if(!load(doc)) {
    return "";
  }

  JsonObject users = doc.as<JsonObject>();
  if(!users[rfidTag].isNull()) {
    return users[rfidTag].as<String>();
  }

  return "";
}

bool RfidUser::setUserName(const String &rfidTag, const String &userName)
{
  if(rfidTag.length() == 0) {
    return false;
  }

  JsonDocument doc;
  if(!load(doc) && LittleFS.exists(RFID_USERS_FILE)) {
    // The file exists but didn't parse -- corrupt JSON. A genuinely missing
    // file is the normal "no mappings yet" case and still starts empty
    // below; v7's JsonDocument grows on demand, so there's no longer a
    // NoMemory/partial-doc case to guard against here.
    DBUGLN("RfidUser: refusing to modify an unreadable mapping file");
    return false;
  }

  // doc.to<JsonObject>() would discard whatever load() just populated, so only
  // reset the root when it isn't already an object (missing file, corrupt JSON).
  JsonObject users = doc.is<JsonObject>() ? doc.as<JsonObject>() : doc.to<JsonObject>();

  if(userName.length() > 0) {
    users[rfidTag] = userName;
  } else {
    users.remove(rfidTag);
  }

  if(doc.overflowed()) {
    DBUGLN("RfidUser: mapping document overflowed, not saving");
    return false;
  }

  return save(doc);
}

bool RfidUser::removeUserName(const String &rfidTag)
{
  return setUserName(rfidTag, "");
}

bool RfidUser::clearAll()
{
  JsonDocument doc;
  doc.to<JsonObject>();
  return save(doc);
}
