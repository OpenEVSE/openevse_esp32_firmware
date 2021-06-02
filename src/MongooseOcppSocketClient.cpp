/*
 * Author: Matthias Akstaller
 * Created: 2021-04-10
 */

#include "MongooseOcppSocketClient.h"
#include "net_manager.h"

/*** To define a custom CA for OCPP please define the flag OCPP_CUSTOM_CA and set define const char *ocpp_ca with the certificate
 * 
 *** Example: 

#define OCPP_CUSTOM_CA
const char *ocpp_ca = "-----BEGIN CERTIFICATE-----\r\n"
"MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/\r\n"
"MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\r\n"
"DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow\r\n"
"PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD\r\n"
"Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\r\n"
"AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O\r\n"
"rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq\r\n"
"OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b\r\n"
"xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw\r\n"
"7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD\r\n"
"aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV\r\n"
"HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG\r\n"
"SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69\r\n"
"ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr\r\n"
"AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz\r\n"
"R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5\r\n"
"JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo\r\n"
"Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ\r\n"
"-----END CERTIFICATE-----\r\n";

 ***/

//see ArduinoMongoose/src/mongoose.c
#ifndef MG_WEBSOCKET_PING_INTERVAL_SECONDS
#define MG_WEBSOCKET_PING_INTERVAL_SECONDS 5
#endif

#define MG_WEBSOCKET_PING_INTERVAL_MS (MG_WEBSOCKET_PING_INTERVAL_SECONDS * 1000)

bool ocppIsConnected = false;
bool ocppConnected() {
    return ocppIsConnected;
}

int checkUrl(const char *url) {
    unsigned int port_i = 0;
    struct mg_str scheme, query, fragment;
    return mg_parse_uri(mg_mk_str(url), &scheme, NULL, NULL, &port_i, NULL, &query, &fragment); //returns 0 on success, false otherwise
}

MongooseOcppSocketClient::MongooseOcppSocketClient(String &ws_url) {
    this->ws_url = String(ws_url);
    
}

MongooseOcppSocketClient::~MongooseOcppSocketClient() {
    if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] Close and destroy connection to "));
    if (DEBUG_OUT) Serial.println(this->ws_url);
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

    if (DEBUG_OUT && ev != 0) {
        Serial.print(F("[MongooseOcppSocketClient] Opcode: "));
        Serial.print(ev);
        Serial.print(F(", flags "));
        Serial.print(nc->flags);
        Serial.println();
    }

    if (ev != MG_EV_POLL && ev != MG_EV_SEND) {
        instance->last_recv = millis();
    }

    switch (ev) {
        case MG_EV_CONNECT: {
            int status = *((int *) ev_data);
            if (DEBUG_OUT && status != 0) {
                Serial.print(F("[MongooseOcppSocketClient] Connection to "));
                instance->printUrl();
                Serial.print(F(" -- Error: "));
                Serial.println(status);
            }
            break;
        }
        case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
            struct http_message *hm = (struct http_message *) ev_data;
            if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] Connection to "));
            if (DEBUG_OUT) instance->printUrl();
            if (hm->resp_code == 101) {
                if (DEBUG_OUT) Serial.print(F(" -- Connected\n"));
                instance->connection_established = true;
                ocppIsConnected = instance->connection_established; //need it static for Wi-Fi dashboard
            } else {
                if (DEBUG_OUT) Serial.print(F(" -- Connection failed! HTTP code "));
                if (DEBUG_OUT) Serial.println(hm->resp_code);
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

            if (DEBUG_OUT) {
                Serial.print(F("[MongooseOcppSocketClient] WS get text: "));
                for (int i = 0; i < wm->size; i++)
                    Serial.print((char) wm->data[i]);
                Serial.println();
            }

            if (!instance->receiveTXT((const char *) wm->data, wm->size)) { //forward message to OcppEngine
                if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] Processing WebSocket input event failed!\n"));
            }
            break;
        }
        case MG_EV_CLOSE: {
            instance->connection_established = false;
            ocppIsConnected = instance->connection_established; //need it static for Wi-Fi dashboard
            instance->nc = NULL; //resources will be free'd by Mongoose
            if (DEBUG_OUT) {
                Serial.print(F("[MongooseOcppSocketClient] Connection to "));
                instance->printUrl();
                Serial.print(F(" -- Closed\n"));
            }
            break;
        }
    }
}

void MongooseOcppSocketClient::loop() {
    maintainWsConn();
}

void MongooseOcppSocketClient::maintainWsConn() {

    const ulong DEBUG_MSG_INTERVAL = 5000;
    if (DEBUG_OUT && millis() - last_debug_message >= DEBUG_MSG_INTERVAL) {
        last_debug_message = millis();

        //WS successfully connected?
        if (!connection_established) {
            if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] WS unconnected\n"));
        } else if (millis() - last_recv >= MG_WEBSOCKET_PING_INTERVAL_MS + WS_UNRESPONSIVE_THRESHOLD_MS) {
            //WS connected but unresponsive
            if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] WS unresponsive\n"));
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

    const ulong RECONNECT_AFTER = 5000; //in ms
    if (millis() - last_reconnection_attempt < RECONNECT_AFTER) {
        return;
    }

    if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] (re-)connect to "));
    if (DEBUG_OUT) Serial.println(this->ws_url);

    struct mg_connect_opts opts;
    Mongoose.getDefaultOpts(&opts);

#if defined(OCPP_CUSTOM_CA)
    opts.ssl_ca_cert = ocpp_ca;
#endif

    opts.error_string = &mongoose_error_string;

    nc = mg_connect_ws_opt(Mongoose.getMgr(), mg_event_callback, this, opts, this->ws_url.c_str(), "ocpp1.6", NULL);

    if (!nc) {
        if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] Failed to connect to URL: "));
        if (DEBUG_OUT) Serial.println(this->ws_url);
    }

    nc->flags |= MG_F_IS_MongooseOcppSocketClient;

    last_reconnection_attempt = millis();
}

void MongooseOcppSocketClient::reconnect(String &ws_url) {
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

bool MongooseOcppSocketClient::sendTXT(String &out) {
    /*
     * Check if the EVSE is able to send the data at the moment. This fuzzy check can be useful to
     * to diagnose connection problems at upper layers. It gives no guarantee that packages were
     * actually sent successfully.
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


void MongooseOcppSocketClient::printUrl() {
    Serial.print(ws_url);
}
