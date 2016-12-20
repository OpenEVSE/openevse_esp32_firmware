#ifndef _EMONESP_WEB_SERVER_H
#define _EMONESP_WEB_SERVER_H

#include <ESP8266WebServer.h>

extern ESP8266WebServer server;
extern String currentfirmware;

extern void web_server_setup();
extern void web_server_loop();

#endif // _EMONESP_WEB_SERVER_H
