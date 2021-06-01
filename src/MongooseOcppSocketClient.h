/*
 * Author: Matthias Akstaller
 * Created: 2021-04-10
 */

#ifndef MONGOOSE_OCPP_SOCKET_CLIENT_H
#define MONGOOSE_OCPP_SOCKET_CLIENT_H

#include <MongooseCore.h>
#define MG_F_IS_MongooseOcppSocketClient MG_F_USER_2

#include <ArduinoOcpp/Core/OcppSocket.h>
#include <ArduinoOcpp/Core/OcppServer.h> //for typedef ReceiveTXTcallback

#define WS_UNRESPONSIVE_THRESHOLD_MS 4000

extern boolean ocppConnected(); //for dashboard

class MongooseOcppSocketClient : public ArduinoOcpp::OcppSocket {
private:
    ArduinoOcpp::ReceiveTXTcallback receiveTXTcallback = [] (const char *, size_t) {return false;};

    String ws_url = String('\0');

    struct mg_connection *nc = NULL;  // Client connection
    
    bool connection_established = false;
    ulong last_reconnection_attempt = 0;

    //is connection responsive?
    ulong last_recv = 0; // Any input from remote peer is seen as indication for responsivitiy

    ulong last_debug_message = 0; 

    void printUrl();

    const char *mongoose_error_string = NULL;
public:

    MongooseOcppSocketClient(String &ws_url);
    
    ~MongooseOcppSocketClient();

    void loop();

    void maintainWsConn();

    bool sendTXT(String &out);
    
    bool receiveTXT(const char* msg, size_t len);

    void setReceiveTXTcallback(ArduinoOcpp::ReceiveTXTcallback &receiveTXT) {
        this->receiveTXTcallback = receiveTXT;
    }; //ReceiveTXTcallback is defined in OcppServer.h

    static void mg_event_callback(struct mg_connection *nc, int ev, void *ev_data, void *user_data);
};

#endif
