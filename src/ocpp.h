/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#ifndef OCPP_H
#define OCPP_H

#include <MicroTasks.h>

#include "evse_man.h"
#include "lcd.h"

#include "MongooseOcppSocketClient.h"

class ArduinoOcppTask: public MicroTasks::Task {
private:
    MongooseOcppSocketClient *ocppSocket = NULL;
    EvseManager *evse;
    LcdTask *lcd;

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
    std::function<void(EvseState&, EvseProperties&)> inferClaimTransactionActive = [] (EvseState&, EvseProperties&) {};        //Transaction engaged and accepted by the CS
    std::function<void(EvseState&, EvseProperties&)> inferClaimTransactionActiveOffline = [] (EvseState&, EvseProperties&) {}; //Transaction request pending. EVSE may choose to start charging
    std::function<void(EvseState&, EvseProperties&)> inferClaimTransactionInactive = [] (EvseState&, EvseProperties&) {};      //No transaction
    std::function<void(EvseState&, EvseProperties&, float charging_limit)> inferClaimSmartCharging = [] (EvseState&, EvseProperties&, float) {};
    
    void loadOcppLibrary();
    bool ocppLibraryLoaded = false;
    void loadEvseBehavior();

    String getCentralSystemUrl();

    static ArduinoOcppTask *instance;

    //helper functions
    static bool operationIsAccepted(JsonObject payload);
    static bool idTagIsAccepted(JsonObject payload);
    static bool idTagIsRejected(JsonObject payload);
protected:

    //hook method of Task
    void setup();

    //hook method of Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    ArduinoOcppTask();
    ~ArduinoOcppTask();

    void begin(EvseManager &evse, LcdTask &lcd);

    void OcppLibrary_loop();
    
    void updateEvseClaim();

    static void notifyReconfigured();
    void reconfigure();

};

#endif
