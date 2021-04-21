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

    /*
     * OCPP state
     */
    float charging_limit = -1.f; //in Watts. chargingLimit < 0 means that there is no Smart Charging (and no restrictions )

    /*
     * SAE J1772 state
     */
    bool vehicleConnected = false;
    bool vehicleCharging = false;

    std::function<void()> onVehicleConnect = [] () {};
    std::function<void()> onVehicleDisconnect = [] () {};

    /*
     * OpenEVSE connector claiming rules
     */
    std::function<void(EvseState&, EvseProperties&)> inferClaimTransactionActive = [] (EvseState&, EvseProperties&) {};
    std::function<void(EvseState&, EvseProperties&)> inferClaimTransactionInactive = [] (EvseState&, EvseProperties&) {};
    std::function<void(EvseState&, EvseProperties&, float charging_limit)> inferClaimSmartCharging = [] (EvseState&, EvseProperties&, float) {};
    
    //helper functions
    static bool operationIsAccepted(JsonObject payload);
protected:

    //hook method of Task
    void setup();

    //hook method of Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    ArduinoOcppTask();
    ~ArduinoOcppTask() = default;

    void begin(String CS_hostname, uint16_t CS_port, String CS_url, EvseManager &evse);
    
    void updateEvseClaim();

    void setOnVehicleConnected(std::function<void()> onVehicleConnect) {
        this->onVehicleConnect = onVehicleConnect;
    }

    void setOnVehicleDisconnect (std::function<void()> onVehicleDisconnect) {
        this->onVehicleDisconnect = onVehicleDisconnect;
    }
};

#endif