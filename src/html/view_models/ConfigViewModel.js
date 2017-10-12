/* global ko, BaseViewModel */
/* exported ConfigViewModel */

function ConfigViewModel(baseEndpoint) {
  "use strict";
  var endpoint = ko.pureComputed(function () { return baseEndpoint() + "/config"; });
  BaseViewModel.call(this, {
    "ssid": "",
    "pass": "",
    "emoncms_server": "data.openevse.com/emoncms",
    "emoncms_apikey": "",
    "emoncms_node": "",
    "emoncms_fingerprint": "",
    "emoncms_enabled": 0,
    "mqtt_server": "",
    "mqtt_topic": "",
    "mqtt_user": "",
    "mqtt_pass": "",
    "mqtt_solar": "",
    "mqtt_grid_ie": "",
    "mqtt_enabled": 0,
    "ohm_enabled": 0,
    "ohmkey": "",
    "www_username": "",
    "www_password": "",
    "firmware": "-",
    "protocol": "-",
    "espflash": 0,
    "diodet": 0,
    "gfcit": 0,
    "groundt": 0,
    "relayt": 0,
    "ventt": 0,
    "tempt": 0,
    "scale": 1,
    "offset": 0,
    "version": "0.0.0"
  }, endpoint);
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;
