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

class MongooseOcppSocketClient : public ArduinoOcpp::OcppSocket {
private:
    ArduinoOcpp::ReceiveTXTcallback receiveTXTcallback = [] (const char *, size_t) {return false;};

    String ws_url = String('\0');

    struct mg_connection *nc = NULL;  // Client connection
    
    bool connection_established = false;
    ulong last_reconnection_attempt = 0;

    //is connection responsive?
    ulong last_recv = 0; // Any input from remote peer is seen as indication for responsivitiy
    ulong last_status_dbg_msg = 0; //print status info to debug output periodically

    const char *mongoose_error_string = NULL;
public:

    MongooseOcppSocketClient(const String &ws_url);
    
    ~MongooseOcppSocketClient() override;

    void loop() override;

    void maintainWsConn();

    bool sendTXT(std::string &out) override;
    
    bool receiveTXT(const char* msg, size_t len);

    void setReceiveTXTcallback(ArduinoOcpp::ReceiveTXTcallback &receiveTXT) override {
        this->receiveTXTcallback = receiveTXT;
    }; //ReceiveTXTcallback is defined in OcppServer.h

    static void mg_event_callback(struct mg_connection *nc, int ev, void *ev_data, void *user_data);

    void reconnect(const String &ws_url);

    static boolean ocppConnected(); //for dashboard

    static bool isValidUrl(const char *url);
};

#endif
