#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_RFID_USER)
#undef ENABLE_DEBUG
#endif

#include "rfid_user.h"
#include "debug.h"
#include <LittleFS.h>

const char* RfidUser::RFID_USERS_FILE = "/rfid_users.json";

#define RFID_USERS_DOC_SIZE 2048

bool RfidUser::load(DynamicJsonDocument &doc)
{
  File file = LittleFS.open(RFID_USERS_FILE, "r");
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

bool RfidUser::save(const DynamicJsonDocument &doc)
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

  if(LittleFS.exists(RFID_USERS_FILE)) {
    LittleFS.remove(RFID_USERS_FILE);
  }
  if(!LittleFS.rename(tempPath, RFID_USERS_FILE)) {
    DBUGLN("Failed to rename RFID users temp file into place");
    return false;
  }

  return true;
}

String RfidUser::getUserName(const String &rfidTag)
{
  if(rfidTag.length() == 0) {
    return "";
  }

  DynamicJsonDocument doc(RFID_USERS_DOC_SIZE);
  if(!load(doc)) {
    return "";
  }

  JsonObject users = doc.as<JsonObject>();
  if(users.containsKey(rfidTag)) {
    return users[rfidTag].as<String>();
  }

  return "";
}

bool RfidUser::setUserName(const String &rfidTag, const String &userName)
{
  if(rfidTag.length() == 0) {
    return false;
  }

  DynamicJsonDocument doc(RFID_USERS_DOC_SIZE);
  if(!load(doc) && LittleFS.exists(RFID_USERS_FILE)) {
    // The file exists but didn't parse — corrupt JSON, or a mapping that has
    // grown past RFID_USERS_DOC_SIZE (ArduinoJson reports NoMemory but still
    // leaves a partially-populated doc). Saving that partial doc would
    // silently drop the rest of the existing mappings, so refuse instead. A
    // genuinely missing file is the normal "no mappings yet" case and still
    // starts empty below.
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

  return save(doc);
}

bool RfidUser::removeUserName(const String &rfidTag)
{
  return setUserName(rfidTag, "");
}

bool RfidUser::clearAll()
{
  DynamicJsonDocument doc(RFID_USERS_DOC_SIZE);
  doc.to<JsonObject>();
  return save(doc);
}
