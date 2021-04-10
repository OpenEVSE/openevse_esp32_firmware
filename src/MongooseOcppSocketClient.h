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
    //std::function<bool(const char*, size_t)> ReceiveTXTcallback;
    //ArduinoOcpp::ReceiveTXTcallback receiveTXT;

    String ws_url = String('\0');

    struct mg_mgr mgr;        // Event manager
    struct mg_connection *c;  // Client connection
public:
    MongooseOcppSocketClient(String &ws_url);
    
    ~MongooseOcppSocketClient() {mg_mgr_free(&mgr);}

    bool sendTXT(String &out);

    void setReceiveTXTcallback(ArduinoOcpp::ReceiveTXTcallback &receiveTXT); //ReceiveTXTcallback is defined in OcppServer.h

};

#endif
