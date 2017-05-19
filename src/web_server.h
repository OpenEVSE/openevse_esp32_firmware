#ifndef _EMONESP_WEB_SERVER_H
#define _EMONESP_WEB_SERVER_H

#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
extern String currentfirmware;

extern void web_server_setup();
extern void web_server_loop();

#endif // _EMONESP_WEB_SERVER_H
