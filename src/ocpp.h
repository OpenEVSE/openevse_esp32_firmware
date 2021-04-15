/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#ifndef OCPP_H
#define OCPP_H

#include <MicroTasks.h>

#include "evse_man.h"

#include "MongooseOcppSocketClient.h"


class MicroTasksCallback : public MicroTasks::Task {
private:
    std::function<void()> callback;
public:
    MicroTasksCallback(std::function<void()> callback) {
        this->callback = callback;
        MicroTask.startTask(this);
    }

    void setup(){ }

    unsigned long loop(MicroTasks::WakeReason reason) {
        if (reason == WakeReason_Event)
            callback();
        return MicroTask.WaitForEvent;
    }
};

class ArduinoOcppTask: public MicroTasks::Task {
private:
    //std::shared_ptr<ArduinoOcpp::OcppSocket> ocppSocket;
    EvseManager *evse;

    MicroTasksCallback bootReadyCallback; //called whenever OpenEVSE runs its boot procedure
protected:

    //hook method of Task
    void setup();

    //hook method of Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    ArduinoOcppTask();
    ~ArduinoOcppTask() = default;

    void begin(String CS_hostname, uint16_t CS_port, String CS_url, EvseManager &evse);
};

#endif