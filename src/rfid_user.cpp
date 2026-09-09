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
  File file = LittleFS.open(RFID_USERS_FILE, "w");
  if(!file) {
    DBUGLN("Failed to open RFID users file for writing");
    return false;
  }

  if(serializeJson(doc, file) == 0) {
    DBUGLN("Failed to write RFID users file");
    file.close();
    return false;
  }

  file.close();
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
  load(doc); // Load existing data, ignore errors; a missing/invalid file starts empty

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
