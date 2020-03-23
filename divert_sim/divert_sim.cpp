#include <iostream>

#include "Console.h"
#include "emonesp.h"
#include "RapiSender.h"
#include "openevse.h"

RapiSender rapiSender(&RAPI_PORT);

long pilot = 0;                       // OpenEVSE Pilot Setting
long state = OPENEVSE_STATE_STARTING; // OpenEVSE State
String mqtt_solar = "";
String mqtt_grid_ie = "";

int main(int, char**) {
    std::cout << "Hello, world!\n";
}

void event_send(String event)
{
}
