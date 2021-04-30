/*
 * Author: Matthias Akstaller
 * Created: 2021-04-10
 */

#ifndef MONGOOSE_OCPP_SOCKET_CLIENT_H
#define MONGOOSE_OCPP_SOCKET_CLIENT_H

#include <MongooseCore.h>

#include <ArduinoOcpp/Core/OcppSocket.h>
#include <ArduinoOcpp/Core/OcppServer.h> //for typedef ReceiveTXTcallback

class MongooseOcppSocketClient : public ArduinoOcpp::OcppSocket {
private:
    ArduinoOcpp::ReceiveTXTcallback receiveTXTcallback = [] (const char *, size_t) {return false;};

    String ws_url = String('\0');

    struct mg_connection *nc;  // Client connection

    bool connection_alive = false;

    void printUrl();

    const char *mongoose_error_string = NULL;
public:

    MongooseOcppSocketClient(String &ws_url);
    
    ~MongooseOcppSocketClient();

    void loop() { }

    bool sendTXT(String &out);
    
    bool receiveTXT(const char* msg, size_t len);

    void setReceiveTXTcallback(ArduinoOcpp::ReceiveTXTcallback &receiveTXT) {
        this->receiveTXTcallback = receiveTXT;
    }; //ReceiveTXTcallback is defined in OcppServer.h

    static void mg_event_callback(struct mg_connection *nc, int ev, void *ev_data, void *user_data);
};

#endif
