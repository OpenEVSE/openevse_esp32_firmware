#ifndef _RFID_USER_H
#define _RFID_USER_H

#include <Arduino.h>
#include <ArduinoJson.h>

class RfidUser
{
private:
  static const char* RFID_USERS_FILE;

public:
  // Load RFID user mappings from file
  static bool load(DynamicJsonDocument &doc);
  
  // Save RFID user mappings to file
  static bool save(const DynamicJsonDocument &doc);
  
  // Get user name for an RFID tag
  static String getUserName(const String &rfidTag);
  
  // Set user name for an RFID tag
  static bool setUserName(const String &rfidTag, const String &userName);
  
  // Remove user name mapping for an RFID tag
  static bool removeUserName(const String &rfidTag);
  
  // Clear all user mappings
  static bool clearAll();
};

#endif // _RFID_USER_H
