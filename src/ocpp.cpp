/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include "debug.h"

#include <ArduinoOcpp.h> // Facade for ArduinoOcpp
#include <ArduinoOcpp/SimpleOcppOperationFactory.h> // define behavior for incoming req messages

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task() {

}

void ArduinoOcppTask::begin(String CS_hostname, uint16_t CS_port, String CS_url, EvseManager *evse) {
    Serial.println("[ArduinoOcppTask] begin! No action ...");
    //OCPP_initialize(CS_hostname, CS_port, CS_url);

    this->evse = evse;

    MicroTask.startTask(this);
}

void ArduinoOcppTask::setup() {
    struct BootReady : MicroTasks::Task {
    virtual void setup() { Serial.print("[BootReady] Listener: setup\n");}
    virtual unsigned long loop(MicroTasks::WakeReason reason) {
      Serial.print("[BootReady] Listener: loop\n");
//      bootNotification("Advanced Series", "OpenEVSE", [](JsonObject payload) {
//        Serial.print("[main] BootNotification successful!\n");
//      });
      return MicroTask.Infinate;
    }
  };

  BootReady bootReadyTask;
  MicroTasks::EventListener bootReadyEvent = &bootReadyTask;
  evse->onBootReady(&bootReadyEvent);

  bootNotification(evse->getFirmwareVersion(), "OpenEVSE", [](JsonObject payload) { //alternative to listener approach above for development
    Serial.print("[main] BootNotification successful!\n");
  });

//  setPowerActiveImportSamplerListener([ &evse = evse]() {
//    return (float) (evse->getAmps() * evse->getVoltage());
//  });

//  setEnergyActiveImportSampler([ this ] () {
//    //return (float) evse->getTotalEnergy();
//    return 1.f;
//  });

    ArduinoOcpp::setOnRemoteStartTransactionReceiveRequestListener([] (JsonObject payload) {

    });
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {
    Serial.println("[ArduinoOcppTask] loop!");
    




    return 5000;
}

