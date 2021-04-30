/*
 * Author: Matthias Akstaller
 * Created: 2021-04-10
 */

#include "MongooseOcppSocketClient.h"

struct mg_connection *nc_last;
MongooseOcppSocketClient *user_data_last;

MongooseOcppSocketClient::MongooseOcppSocketClient(String &ws_url) {
    this->ws_url = String(ws_url);

    if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] Create connection to "));
    if (DEBUG_OUT) Serial.println(this->ws_url);

    struct mg_connect_opts opts;
    Mongoose.getDefaultOpts(&opts);

    opts.error_string = &mongoose_error_string;

    //nc = mg_connect_ws(Mongoose.getMgr(), mg_event_callback, this, this->ws_url.c_str(), "ocpp1.6", NULL);
    nc = mg_connect_ws_opt(Mongoose.getMgr(), mg_event_callback, this, opts, this->ws_url.c_str(), "ocpp1.6", NULL);
    nc_last = nc;
    user_data_last = this;

    if (!nc) {
        Serial.print(F("[MongooseOcppSocketClient] Failed to connect to URL: "));
        Serial.println(this->ws_url);
    }
}

MongooseOcppSocketClient::~MongooseOcppSocketClient() {
    if (DEBUG_OUT) Serial.print(F("[MongooseOcppSocketClient] Close and destroy connection to "));
    if (DEBUG_OUT) Serial.println(this->ws_url);
    const char *msg = "socket closed by client";
    mg_send_websocket_frame(nc, WEBSOCKET_OP_CLOSE, msg, strlen(msg));
}

void MongooseOcppSocketClient::mg_event_callback(struct mg_connection *nc, int ev, void *ev_data, void *user_data) {
    if (user_data != user_data_last) Serial.print("[MongooseOcppSocketClient] UNEQUAL user_data_last\n");
    if (nc != nc_last) Serial.print("[MongooseOcppSocketClient] UNEQUAL nc_last\n");

    MongooseOcppSocketClient *instance = static_cast<MongooseOcppSocketClient*>(user_data);

    if (DEBUG_OUT && ev != 0) {
        Serial.print(F("[MongooseOcppSocketClient] Opcode: "));
        Serial.print(ev);
        Serial.print(F(", flags "));
        Serial.print(nc->flags);
        Serial.println();
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
                instance->connection_alive = true;
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
                Serial.print(F("[OcppClientSocket] Processing WebSocket input event failed!\n"));
            }
            break;
        }
        case MG_EV_CLOSE: {
            if (DEBUG_OUT) {
                Serial.print(F("[MongooseOcppSocketClient] Connection to "));
                instance->printUrl();
                Serial.print(F(" -- Closed by peer request\n"));
            }
            break;
        }
    }
}

bool MongooseOcppSocketClient::sendTXT(String &out) {
    if (!connection_alive)
        return false;
    mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, out.c_str(), out.length());
    return true; //no, return if sending was successful
}

bool MongooseOcppSocketClient::receiveTXT(const char* msg, size_t len) {
    return receiveTXTcallback(msg, len);
}

void MongooseOcppSocketClient::printUrl() {
    Serial.print(ws_url);
}
