#include "emonesp.h"
#include "config.h"

#include <Arduino.h>
#include <EEPROM.h>             // Save config settings

// Wifi Network Strings
String esid = "";
String epass = "";

// Web server authentication (leave blank for none)
String www_username = "";
String www_password = "";

// EMONCMS SERVER strings
String emoncms_server = "";
String emoncms_node = "";
String emoncms_apikey = "";
String emoncms_fingerprint = "";

// MQTT Settings
String mqtt_server = "";
String mqtt_topic = "";
String mqtt_user = "";
String mqtt_pass = "";
String mqtt_solar = "";
String mqtt_grid_ie = "";

// Ohm Connect Settings
String ohm = "";

// Flags
uint32_t flags;

#define EEPROM_ESID_SIZE              32
#define EEPROM_EPASS_SIZE             64
#define EEPROM_EMON_API_KEY_SIZE      33
#define EEPROM_EMON_SERVER_SIZE       45
#define EEPROM_EMON_NODE_SIZE         32
#define EEPROM_MQTT_SERVER_SIZE       45
#define EEPROM_MQTT_TOPIC_SIZE        32
#define EEPROM_MQTT_USER_SIZE         32
#define EEPROM_MQTT_PASS_SIZE         64
#define EEPROM_MQTT_SOLAR_SIZE        30
#define EEPROM_MQTT_GRID_IE_SIZE      30
#define EEPROM_EMON_FINGERPRINT_SIZE  60
#define EEPROM_WWW_USER_SIZE          16
#define EEPROM_WWW_PASS_SIZE          16
#define EEPROM_OHM_KEY_SIZE           10
#define EEPROM_FLAGS_SIZE             4
#define EEPROM_SIZE                   1024

#define EEPROM_ESID_START             0
#define EEPROM_ESID_END               (EEPROM_ESID_START + EEPROM_ESID_SIZE)
#define EEPROM_EPASS_START            EEPROM_ESID_END
#define EEPROM_EPASS_END              (EEPROM_EPASS_START + EEPROM_EPASS_SIZE)
#define EEPROM_EMON_SERVER_START      EEPROM_EPASS_END + 32 /* EEPROM_EMON_API_KEY used to be stored before this */
#define EEPROM_EMON_SERVER_END        (EEPROM_EMON_SERVER_START + EEPROM_EMON_SERVER_SIZE)
#define EEPROM_EMON_NODE_START        EEPROM_EMON_SERVER_END
#define EEPROM_EMON_NODE_END          (EEPROM_EMON_NODE_START + EEPROM_EMON_NODE_SIZE)
#define EEPROM_MQTT_SERVER_START      EEPROM_EMON_NODE_END
#define EEPROM_MQTT_SERVER_END        (EEPROM_MQTT_SERVER_START + EEPROM_MQTT_SERVER_SIZE)
#define EEPROM_MQTT_TOPIC_START       EEPROM_MQTT_SERVER_END
#define EEPROM_MQTT_TOPIC_END         (EEPROM_MQTT_TOPIC_START + EEPROM_MQTT_TOPIC_SIZE)
#define EEPROM_MQTT_USER_START        EEPROM_MQTT_TOPIC_END
#define EEPROM_MQTT_USER_END          (EEPROM_MQTT_USER_START + EEPROM_MQTT_USER_SIZE)
#define EEPROM_MQTT_PASS_START        EEPROM_MQTT_USER_END
#define EEPROM_MQTT_PASS_END          (EEPROM_MQTT_PASS_START + EEPROM_MQTT_PASS_SIZE)
#define EEPROM_MQTT_SOLAR_START       EEPROM_MQTT_PASS_END
#define EEPROM_MQTT_SOLAR_END         (EEPROM_MQTT_SOLAR_START + EEPROM_MQTT_SOLAR_SIZE)
#define EEPROM_MQTT_GRID_IE_START     EEPROM_MQTT_SOLAR_END
#define EEPROM_MQTT_GRID_IE_END       (EEPROM_MQTT_GRID_IE_START + EEPROM_MQTT_GRID_IE_SIZE)
#define EEPROM_EMON_FINGERPRINT_START EEPROM_MQTT_GRID_IE_END
#define EEPROM_EMON_FINGERPRINT_END   (EEPROM_EMON_FINGERPRINT_START + EEPROM_EMON_FINGERPRINT_SIZE)
#define EEPROM_WWW_USER_START         EEPROM_EMON_FINGERPRINT_END
#define EEPROM_WWW_USER_END           (EEPROM_WWW_USER_START + EEPROM_WWW_USER_SIZE)
#define EEPROM_WWW_PASS_START         EEPROM_WWW_USER_END
#define EEPROM_WWW_PASS_END           (EEPROM_WWW_PASS_START + EEPROM_WWW_PASS_SIZE)
#define EEPROM_OHM_KEY_START          EEPROM_WWW_PASS_END
#define EEPROM_OHM_KEY_END            (EEPROM_OHM_KEY_START + EEPROM_OHM_KEY_SIZE)
#define EEPROM_FLAGS_START            EEPROM_OHM_KEY_END
#define EEPROM_FLAGS_END              (EEPROM_FLAGS_START + EEPROM_FLAGS_SIZE)
#define EEPROM_EMON_API_KEY_START     EEPROM_FLAGS_END
#define EEPROM_EMON_API_KEY_END       (EEPROM_EMON_API_KEY_START + EEPROM_EMON_API_KEY_SIZE)
#define EEPROM_CONFIG_END             EEPROM_EMON_API_KEY_END

#if EEPROM_CONFIG_END > EEPROM_SIZE
#error EEPROM_SIZE too small
#endif

#define CHECKSUM_SEED 128

// -------------------------------------------------------------------
// Reset EEPROM, wipes all settings
// -------------------------------------------------------------------
void
ResetEEPROM() {
  EEPROM.begin(EEPROM_SIZE);

  //DEBUG.println("Erasing EEPROM");
  for (int i = 0; i < EEPROM_SIZE; ++i) {
    EEPROM.write(i, 0xff);
    //DEBUG.print("#");
  }
  EEPROM.end();
}

void
EEPROM_read_string(int start, int count, String & val, String defaultVal = "") {
  byte checksum = CHECKSUM_SEED;
  for (int i = 0; i < count - 1; ++i) {
    byte c = EEPROM.read(start + i);
    if (c != 0 && c != 255) {
      checksum ^= c;
      val += (char) c;
    } else {
      break;
    }
  }

  // Check the checksum
  byte c = EEPROM.read(start + (count - 1));
  DBUGF("Got '%s' %d == %d @ %d:%d", val.c_str(), c, checksum, start, count);
  if(c != checksum) {
    DBUGF("Using default '%s'", defaultVal.c_str());
    val = defaultVal;
  }
}

void
EEPROM_write_string(int start, int count, String val) {
  byte checksum = CHECKSUM_SEED;
  for (int i = 0; i < count - 1; ++i) {
    if (i < val.length()) {
      checksum ^= val[i];
      EEPROM.write(start + i, val[i]);
    } else {
      EEPROM.write(start + i, 0);
    }
  }
  EEPROM.write(start + (count - 1), checksum);
  DBUGF("Saved '%s' %d @ %d:%d", val.c_str(), checksum, start, count);
}

void
EEPROM_read_uint24(int start, uint32_t & val, uint32_t defaultVal = 0) {
  byte checksum = CHECKSUM_SEED;
  val = 0;
  for (int i = 0; i < 3; ++i) {
    byte c = EEPROM.read(start + i);
    checksum ^= c;
    val = (val << 8) | c;
  }

  // Check the checksum
  byte c = EEPROM.read(start + 3);
  DBUGF("Got '%06x'  %d == %d @ %d:4", val, c, checksum, start);
  if(c != checksum) {
    DBUGF("Using default '%06x'", defaultVal);
    val = defaultVal;
  }
}

void
EEPROM_write_uint24(int start, uint32_t value) {
  byte checksum = CHECKSUM_SEED;
  uint32_t val = value;
  for (int i = 2; i >= 0; --i) {
    byte c = val & 0xff;
    val = val >> 8;
    checksum ^= c;
    EEPROM.write(start + i, c);
  }
  EEPROM.write(start + 3, checksum);
  DBUGF("Saved '%06x' %d @ %d:4", value, checksum, start);
}

// -------------------------------------------------------------------
// Load saved settings from EEPROM
// -------------------------------------------------------------------
void
config_load_settings() {
  DBUGLN("Loading config");
  EEPROM.begin(EEPROM_SIZE);

  // Load WiFi values
  EEPROM_read_string(EEPROM_ESID_START, EEPROM_ESID_SIZE, esid);
  EEPROM_read_string(EEPROM_EPASS_START, EEPROM_EPASS_SIZE, epass);

  // EmonCMS settings
  EEPROM_read_string(EEPROM_EMON_API_KEY_START, EEPROM_EMON_API_KEY_SIZE,
                     emoncms_apikey);
  EEPROM_read_string(EEPROM_EMON_SERVER_START, EEPROM_EMON_SERVER_SIZE,
                     emoncms_server, "data.openevse.com/emoncms");
  EEPROM_read_string(EEPROM_EMON_NODE_START, EEPROM_EMON_NODE_SIZE,
                     emoncms_node, "openevse");
  EEPROM_read_string(EEPROM_EMON_FINGERPRINT_START, EEPROM_EMON_FINGERPRINT_SIZE,
                     emoncms_fingerprint, "7D:82:15:BE:D7:BC:72:58:87:7D:8E:40:D4:80:BA:1A:9F:8B:8D:DA");

  // MQTT settings
  EEPROM_read_string(EEPROM_MQTT_SERVER_START, EEPROM_MQTT_SERVER_SIZE,
                     mqtt_server, "emonpi");
  EEPROM_read_string(EEPROM_MQTT_TOPIC_START, EEPROM_MQTT_TOPIC_SIZE,
                     mqtt_topic, "openevse");
  EEPROM_read_string(EEPROM_MQTT_USER_START, EEPROM_MQTT_USER_SIZE,
                     mqtt_user, "emonpi");
  EEPROM_read_string(EEPROM_MQTT_PASS_START, EEPROM_MQTT_PASS_SIZE,
                     mqtt_pass, "emonpimqtt2016");
  EEPROM_read_string(EEPROM_MQTT_SOLAR_START, EEPROM_MQTT_SOLAR_SIZE,
                     mqtt_solar);
  EEPROM_read_string(EEPROM_MQTT_GRID_IE_START, EEPROM_MQTT_GRID_IE_SIZE,
                     mqtt_grid_ie, "emon/emonpi/power");

  // Web server credentials
  EEPROM_read_string(EEPROM_WWW_USER_START, EEPROM_WWW_USER_SIZE,
                     www_username, "");
  EEPROM_read_string(EEPROM_WWW_PASS_START, EEPROM_WWW_PASS_SIZE,
                     www_password, "");

  // Ohm Connect Settings
  EEPROM_read_string(EEPROM_OHM_KEY_START, EEPROM_OHM_KEY_SIZE, ohm);

  // Flags
  EEPROM_read_uint24(EEPROM_FLAGS_START, flags, 0);

  EEPROM.end();
}

void
config_save_emoncms(bool enable, String server, String node, String apikey,
                    String fingerprint)
{
  EEPROM.begin(EEPROM_SIZE);

  flags = flags & ~CONFIG_SERVICE_EMONCMS;
  if(enable) {
    flags |= CONFIG_SERVICE_EMONCMS;
  }

  emoncms_server = server;
  emoncms_node = node;
  emoncms_apikey = apikey;
  emoncms_fingerprint = fingerprint;

  // save apikey to EEPROM
  EEPROM_write_string(EEPROM_EMON_API_KEY_START, EEPROM_EMON_API_KEY_SIZE,
                      emoncms_apikey);

  // save emoncms server to EEPROM max 45 characters
  EEPROM_write_string(EEPROM_EMON_SERVER_START, EEPROM_EMON_SERVER_SIZE,
                      emoncms_server);

  // save emoncms node to EEPROM max 32 characters
  EEPROM_write_string(EEPROM_EMON_NODE_START, EEPROM_EMON_NODE_SIZE,
                      emoncms_node);

  // save emoncms HTTPS fingerprint to EEPROM max 60 characters
  EEPROM_write_string(EEPROM_EMON_FINGERPRINT_START,
                      EEPROM_EMON_FINGERPRINT_SIZE, emoncms_fingerprint);

  EEPROM_write_uint24(EEPROM_FLAGS_START, flags);

  EEPROM.end();
}

void
config_save_mqtt(bool enable, String server, String topic, String user, String pass, String solar, String grid_ie)
{
  EEPROM.begin(EEPROM_SIZE);

  flags = flags & ~CONFIG_SERVICE_MQTT;
  if(enable) {
    flags |= CONFIG_SERVICE_MQTT;
  }

  mqtt_server = server;
  mqtt_topic = topic;
  mqtt_user = user;
  mqtt_pass = pass;
  mqtt_solar = solar;
  mqtt_grid_ie = grid_ie;

  EEPROM_write_string(EEPROM_MQTT_SERVER_START, EEPROM_MQTT_SERVER_SIZE,
                      mqtt_server);
  EEPROM_write_string(EEPROM_MQTT_TOPIC_START, EEPROM_MQTT_TOPIC_SIZE,
                      mqtt_topic);
  EEPROM_write_string(EEPROM_MQTT_USER_START, EEPROM_MQTT_USER_SIZE,
                      mqtt_user);
  EEPROM_write_string(EEPROM_MQTT_PASS_START, EEPROM_MQTT_PASS_SIZE,
                      mqtt_pass);
  EEPROM_write_string(EEPROM_MQTT_SOLAR_START, EEPROM_MQTT_SOLAR_SIZE, mqtt_solar);
  EEPROM_write_string(EEPROM_MQTT_GRID_IE_START, EEPROM_MQTT_GRID_IE_SIZE, mqtt_grid_ie);

  EEPROM_write_uint24(EEPROM_FLAGS_START, flags);

  EEPROM.end();
}

void
config_save_admin(String user, String pass) {
  EEPROM.begin(EEPROM_SIZE);

  www_username = user;
  www_password = pass;

  EEPROM_write_string(EEPROM_WWW_USER_START, EEPROM_WWW_USER_SIZE, user);
  EEPROM_write_string(EEPROM_WWW_PASS_START, EEPROM_WWW_PASS_SIZE, pass);

  EEPROM.end();
}

void
config_save_wifi(String qsid, String qpass)
{
  EEPROM.begin(EEPROM_SIZE);

  esid = qsid;
  epass = qpass;

  EEPROM_write_string(EEPROM_ESID_START, EEPROM_ESID_SIZE, qsid);
  EEPROM_write_string(EEPROM_EPASS_START, EEPROM_EPASS_SIZE, qpass);

  EEPROM.end();
}

void
config_save_ohm(bool enable, String qohm)
{
  EEPROM.begin(EEPROM_SIZE);

  flags = flags & ~CONFIG_SERVICE_OHM;
  if(enable) {
    flags |= CONFIG_SERVICE_OHM;
  }

  ohm = qohm;

  EEPROM_write_string(EEPROM_OHM_KEY_START, EEPROM_OHM_KEY_SIZE, qohm);

  EEPROM_write_uint24(EEPROM_FLAGS_START, flags);

  EEPROM.end();
}

void
config_save_flags(uint32_t newFlags) {
  if(flags != newFlags)
  {
    EEPROM.begin(EEPROM_SIZE);

    flags = newFlags;

    EEPROM_write_uint24(EEPROM_FLAGS_START, flags);

    EEPROM.end();
  }
}

void
config_reset() {
  ResetEEPROM();
}
