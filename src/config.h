#ifndef _EMONESP_CONFIG_H
#define _EMONESP_CONFIG_H

#include <Arduino.h>

// Global config varables

// Wifi Network Strings
extern String esid;
extern String epass;

// Web server authentication (leave blank for none)
extern String www_username;
extern String www_password;

// EMONCMS SERVER strings
extern String emoncms_server;
extern String emoncms_node;
extern String emoncms_apikey;
extern String emoncms_fingerprint;

// MQTT Settings
extern String mqtt_server;
extern String mqtt_topic;
extern String mqtt_user;
extern String mqtt_pass;

//Ohm Connect Settings
extern String ohm;

// -------------------------------------------------------------------
// Load saved settings from config
// -------------------------------------------------------------------
extern void config_load_settings();

extern void config_save_emoncms(String server, String node, String apikey, String fingerprint);
extern void config_save_mqtt(String server, String topic, String user, String pass);
extern void config_save_admin(String user, String pass);
extern void config_save_wifi(String qsid, String qpass);
extern void config_save_ohm(String qohm);

extern void config_reset();

#endif // _EMONESP_CONFIG_H
