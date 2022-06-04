/*
 * Author: Matthias Akstaller
 * Created: 2021-04-10
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_OCPPSOCKET)
#undef ENABLE_DEBUG
#endif

#include "MongooseOcppSocketClient.h"
#include "net_manager.h"
#include "debug.h"

/*** OCPP Security Profile 2 (TLS with Basic Authentication) -- Manual configuration
 * 
 * To use this charger with a Profile 2 OCPP backend, define the Certificate and
 * HTTP basic authentication token like in the example below.
 * 
 *** Example: 

#define OCPP_CUSTOM_CA    //enable TLS certificte

const char *ocpp_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT
DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow
PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD
Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB
AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O
rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq
OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b
xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw
7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD
aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV
HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG
SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69
ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr
AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz
R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5
JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo
Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ
-----END CERTIFICATE-----
)EOF";

//Enable and define HTTP basic authentication token. A description of how to form the
//token can be found in the specifiaction, part "OCPP 1.6 Specification - JSON.pdf",
//page 16.
#define OCPP_BASIC_AUTH_TOKEN "QUwxMDAwOgABAgMEBQYH////////////////"

 ***/

#define WS_UNRESPONSIVE_THRESHOLD_MS 4000UL

#define DEBUG_MSG_INTERVAL 5000UL 

#define RECONNECT_AFTER 5000UL  //pause interval between two reconnection attempts

//see ArduinoMongoose/src/mongoose.c
#ifndef MG_WEBSOCKET_PING_INTERVAL_SECONDS
#define MG_WEBSOCKET_PING_INTERVAL_SECONDS 5
#endif

#define MG_WEBSOCKET_PING_INTERVAL_MS (MG_WEBSOCKET_PING_INTERVAL_SECONDS * 1000UL)

bool ocppIsConnected = false;

MongooseOcppSocketClient::MongooseOcppSocketClient(const String &ws_url) {
    this->ws_url = String(ws_url);
    
}

MongooseOcppSocketClient::~MongooseOcppSocketClient() {
    DBUG(F("[MongooseOcppSocketClient] Close and destroy connection to "));
    DBUGLN(this->ws_url);
    if (nc) {
        connection_established = false;
        ocppIsConnected = connection_established; //need it static for Wi-Fi dashboard
        const char *msg = "socket closed by client";
        mg_send_websocket_frame(nc, WEBSOCKET_OP_CLOSE, msg, strlen(msg));
        nc = NULL;
    }
}

void MongooseOcppSocketClient::mg_event_callback(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {

    MongooseOcppSocketClient *instance = NULL;
    
    if (nc->flags & MG_F_IS_WEBSOCKET && nc->flags & MG_F_IS_MongooseOcppSocketClient) {
        instance = static_cast<MongooseOcppSocketClient*>(user_data);
    } else {
        return;
    }

    if (ev != MG_EV_POLL && ev != MG_EV_SEND) {
        instance->last_recv = millis();
    }

    switch (ev) {
        case MG_EV_CONNECT: {
            int status = *((int *) ev_data);
            if (status != 0) {
                DBUG(F("[MongooseOcppSocketClient] Connection to "));
                DBUG(instance->ws_url);
                DBUG(F(" -- Error: "));
                DBUGLN(status);
            }
            break;
        }
        case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
            struct http_message *hm = (struct http_message *) ev_data;
            DBUG(F("[MongooseOcppSocketClient] Connection to "));
            DBUG(instance->ws_url);
            if (hm->resp_code == 101) {
                DBUGLN(F(" -- Connected"));
                instance->connection_established = true;
                ocppIsConnected = instance->connection_established; //need it static for Wi-Fi dashboard
            } else {
                DBUG(F(" -- Connection failed! HTTP code "));
                DBUGLN(hm->resp_code);
                /* Connection will be closed after this. */
            }
            break;
        }
        case MG_EV_POLL: {
            /* Nothing to do here. OCPP engine has own loop-function */
            break;
        }
        case MG_EV_WEBSOCKET_FRAME: {
            struct websocket_message *wm = (struct websocket_message *) ev_data;

            if (!instance->receiveTXT((const char *) wm->data, wm->size)) { //forward message to OcppEngine
                DBUGLN(F("[MongooseOcppSocketClient] Processing WebSocket input event failed!"));
            }
            break;
        }
        case MG_EV_CLOSE: {
            instance->connection_established = false;
            ocppIsConnected = instance->connection_established; //need it static for Wi-Fi dashboard
            instance->nc = NULL; //resources will be free'd by Mongoose
            DBUG(F("[MongooseOcppSocketClient] Connection to "));
            DBUG(instance->ws_url);
            DBUGLN(F(" -- Closed"));
            break;
        }
    }
}

void MongooseOcppSocketClient::loop() {
    maintainWsConn();
}

void MongooseOcppSocketClient::maintainWsConn() {

    if (millis() - last_status_dbg_msg >= DEBUG_MSG_INTERVAL) {
        last_status_dbg_msg = millis();

        //WS successfully connected?
        if (!connection_established) {
            DBUGLN(F("[MongooseOcppSocketClient] WS unconnected"));
        } else if (millis() - last_recv >= MG_WEBSOCKET_PING_INTERVAL_MS + WS_UNRESPONSIVE_THRESHOLD_MS) {
            //WS connected but unresponsive
            DBUGLN(F("[MongooseOcppSocketClient] WS unresponsive"));
        }
    }

    if (nc != NULL) { //connection pointer != NULL means that the socket is still open
        return;
    }

    if (!net_is_connected()) {
        return;
    }

    if (ws_url.isEmpty()) {
        return;
    }

    if (millis() - last_reconnection_attempt < RECONNECT_AFTER) {
        return;
    }

    DBUG(F("[MongooseOcppSocketClient] (re-)connect to "));
    DBUGLN(this->ws_url);

    struct mg_connect_opts opts;
    Mongoose.getDefaultOpts(&opts);

#if defined(OCPP_CUSTOM_CA)
    opts.ssl_ca_cert = ocpp_ca;
#endif

    opts.error_string = &mongoose_error_string;

    const char *extra_headers = nullptr;

#if defined(OCPP_BASIC_AUTH_TOKEN)
    extra_headers = "Authorization: Basic " OCPP_BASIC_AUTH_TOKEN "\n";
#endif

    nc = mg_connect_ws_opt(Mongoose.getMgr(), mg_event_callback, this, opts, this->ws_url.c_str(), "ocpp1.6", extra_headers);

    if (!nc) {
        DBUG(F("[MongooseOcppSocketClient] Failed to connect to URL: "));
        DBUGLN(this->ws_url);
    }

    nc->flags |= MG_F_IS_MongooseOcppSocketClient;

    last_reconnection_attempt = millis();
}

void MongooseOcppSocketClient::reconnect(const String &ws_url) {
    this->ws_url = String(ws_url);

    connection_established = false;
    ocppIsConnected = connection_established; //need it static for Wi-Fi dashboard
    if (nc) {
        const char *msg = "socket closed by client";
        mg_send_websocket_frame(nc, WEBSOCKET_OP_CLOSE, msg, strlen(msg));
        nc = NULL;
    }

    maintainWsConn();
}

bool MongooseOcppSocketClient::sendTXT(std::string &out) {
    /*
     * Check if the EVSE is able to send the data at the moment. This fuzzy check can be useful to
     * to diagnose connection problems at upper layers. It gives no guarantee that packages will
     * actually be sent successfully.
     */
    if (!nc || !connection_established || !net_is_connected())
        return false;

    if (millis() - last_recv >= MG_WEBSOCKET_PING_INTERVAL_MS + WS_UNRESPONSIVE_THRESHOLD_MS) {
        //unresponsive; wait until next WS ping-pong succeeds
        return false;
    }

    if (nc->send_mbuf.len > 0) {
        //Something else is already in the queue. This is a strong indicator that the device
        //cannot send at the moment
        return false;
    }

    mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, out.c_str(), out.length());

    return true; //message was handed over to underlying HTTP-layer
}

bool MongooseOcppSocketClient::receiveTXT(const char* msg, size_t len) {
    return receiveTXTcallback(msg, len);
}

bool MongooseOcppSocketClient::ocppConnected() {
    return ocppIsConnected;
}

bool MongooseOcppSocketClient::isValidUrl(const char *url) {
    //URL must start with ws: or wss:
    if (url[0] != 'W' && url[0] != 'w')
        return false;
    if (url[1] != 'S' && url[1] != 's')
        return false;

    if (url[2] == 'S' || url[2] == 's') {
        if (url[3] != ':')
            return false;
        //else: passed
    } else if (url[2] != ':') {
        return false;
    }
    //passed

    unsigned int port_i = 0;
    struct mg_str scheme, query, fragment;
    return mg_parse_uri(mg_mk_str(url), &scheme, NULL, NULL, &port_i, NULL, &query, &fragment) == 0; //mg_parse_uri returns 0 on success, false otherwise
}
