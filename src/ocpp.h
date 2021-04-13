/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#ifndef OCPP_H
#define OCPP_H

#include <MicroTasks.h>

#include "MongooseOcppSocketClient.h"

class ArduinoOcppTask: public MicroTasks::Task {
private:
    std::shared_ptr<ArduinoOcpp::OcppSocket> ocppSocket;
protected:

    //hook method of Task
    void setup();

    //hook method of Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    ArduinoOcppTask();
    ~ArduinoOcppTask() = default;

    void begin(String CS_hostname, uint16_t CS_port, String CS_url);
};

#endif