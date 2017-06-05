// Work out the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
//var baseHost = 'openevse.local';
//var baseHost = '192.168.4.1';
var baseEndpoint = 'http://' + baseHost;

var statusupdate = false;
var selected_network_ssid = "";
var lastmode = "";
var ipaddress = "";
var divertmode = 0;

// Convert string to number, divide by scale, return result
// as a string with specified precision
function scaleString(string, scale, precision) {
  var tmpval = parseInt(string) / scale;
  return tmpval.toFixed(precision);
}

function BaseViewModel(defaults, remoteUrl, mappings = {}) {
  var self = this;
  self.remoteUrl = remoteUrl;

  // Observable properties
  ko.mapping.fromJS(defaults, mappings, self);
  self.fetching = ko.observable(false);
}

BaseViewModel.prototype.update = function (after = function () { }) {
  var self = this;
  self.fetching(true);
  $.get(self.remoteUrl, function (data) {
    ko.mapping.fromJS(data, self);
  }, 'json').always(function () {
    self.fetching(false);
    after();
  });
};


function StatusViewModel() {
  var self = this;

  BaseViewModel.call(self, {
    "mode": "ERR",
    "networks": [],
    "rssi": [],
    "srssi": "",
    "ipaddress": "",
    "packets_sent": "",
    "packets_success": "",
    "emoncms_connected": "",
    "mqtt_connected": "",
    "ohm_hour": "",
    "free_heap": ""
  }, baseEndpoint + '/status');

  // Some devired values
  self.isWifiClient = ko.pureComputed(function () {
    return ("STA" == self.mode()) || ("STA+AP" == self.mode());
  });
  self.isWifiAccessPoint = ko.pureComputed(function () {
    return ("AP" == self.mode()) || ("STA+AP" == self.mode());
  });
  self.fullMode = ko.pureComputed(function () {
    switch (self.mode()) {
      case "AP":
        return "Access Point (AP)";
      case "STA":
        return "Client (STA)";
      case "STA+AP":
        return "Client + Access Point (STA+AP)";
    }

    return "Unknown (" + self.mode() + ")";
  });
}
StatusViewModel.prototype = Object.create(BaseViewModel.prototype);
StatusViewModel.prototype.constructor = StatusViewModel;

function ConfigViewModel() {
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
    "divert_enabled": 0,
    "www_username": "",
    "www_password": "",
    "firmware": "-",
    "protocol": "-",
    "espflash": "",
    "diodet": "",
    "gfcit": "",
    "groundt": "",
    "relayt": "",
    "ventt": "",
    "tempt": "",
    "service": "",
    "l1min": "-",
    "l1max": "-",
    "l2min": "-",
    "l2max": "-",
    "scale": "-",
    "offset": "-",
    "gfcicount": "-",
    "nogndcount": "-",
    "stuckcount": "-",
    "kwhlimit": "",
    "timelimit": "",
    "version": "0.0.0",
    "divertmode": "0"
  }, baseEndpoint + '/config');
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;

function RapiViewModel() {
  BaseViewModel.call(this, {
    "comm_sent": "0",
    "comm_success": "0",
    "amp": "0",
    "pilot": "0",
    "temp1": "0",
    "temp2": "0",
    "temp3": "0",
    "estate": "Unknown",
    "wattsec": "0",
    "watthour": "0"
  }, baseEndpoint + '/rapiupdate');

  this.rapiSend = ko.observable(false);
  this.cmd = ko.observable('');
  this.ret = ko.observable('');
}
RapiViewModel.prototype = Object.create(BaseViewModel.prototype);
RapiViewModel.prototype.constructor = RapiViewModel;
RapiViewModel.prototype.send = function() {
  var self = this;
  self.rapiSend(true);
  $.get(baseEndpoint + '/r?json=1&rapi='+encodeURI(self.cmd()), function (data) {
    self.ret('>'+data.ret);
    self.cmd(data.cmd);
  }, 'json').always(function () {
    self.rapiSend(false);
  });
};

function OpenEvseViewModel() {
  var self = this;

  self.config = new ConfigViewModel();
  self.status = new StatusViewModel();
  self.rapi = new RapiViewModel();

  self.initialised = ko.observable(false);
  self.updating = ko.observable(false);

  var updateTimer = null;
  var updateTime = 1 * 1000;

  // Tabs
  self.tab = ko.observable('system');
  self.isSystem = ko.pureComputed(function() { return 'system' === self.tab(); });
  self.isServices = ko.pureComputed(function() { return 'services' === self.tab(); });
  self.isStatus = ko.pureComputed(function() { return 'status' === self.tab(); });
  self.isRapi = ko.pureComputed(function() { return 'rapi' === self.tab(); });
  self.isMode = ko.pureComputed(function() { return 'mode' === self.tab(); });

  // Upgrade URL
  self.upgradeUrl = ko.observable('about:blank');

  // -----------------------------------------------------------------------
  // Initialise the app
  // -----------------------------------------------------------------------
  self.start = function () {
    self.updating(true);
    self.config.update(function () {
      self.status.update(function () {
        self.rapi.update(function () {
          self.initialised(true);
          updateTimer = setTimeout(self.update, updateTime);
          self.upgradeUrl(baseEndpoint + '/update');
          self.updating(false);
        });
      });
    });
  };

  // -----------------------------------------------------------------------
  // Get the updated state from the ESP
  // -----------------------------------------------------------------------
  self.update = function () {
    if (self.updating()) {
      return;
    }
    self.updating(true);
    if (null !== updateTimer) {
      clearTimeout(updateTimer);
      updateTimer = null;
    }
    self.status.update(function () {
      self.rapi.update(function () {
        updateTimer = setTimeout(self.update, updateTime);
        self.updating(false);
      });
    });
  };

  self.wifiConnecting = ko.observable(false);
  self.status.mode.subscribe(function (newValue) {
    if(newValue === "STA+AP" || newValue === "STA") {
      self.wifiConnecting(false);
    }
  });

  // -----------------------------------------------------------------------
  // Event: WiFi Connect
  // -----------------------------------------------------------------------
  self.saveNetworkFetching = ko.observable(false);
  self.saveNetworkSuccess = ko.observable(false);
  self.saveNetwork = function () {
    if (self.config.ssid() === "") {
      alert("Please select network");
    } else {
      self.saveNetworkFetching(true);
      self.saveNetworkSuccess(false);
      $.post(baseEndpoint + "/savenetwork", { ssid: self.config.ssid(), pass: self.config.pass() }, function (data) {
          self.saveNetworkSuccess(true);
          self.wifiConnecting(true);
        }).fail(function () {
          alert("Failed to save WiFi config");
        }).always(function () {
          self.saveNetworkFetching(false);
        });
    }
  };

  // -----------------------------------------------------------------------
  // Event: Admin save
  // -----------------------------------------------------------------------
  self.saveAdminFetching = ko.observable(false);
  self.saveAdminSuccess = ko.observable(false);
  self.saveAdmin = function () {
    self.saveAdminFetching(true);
    self.saveAdminSuccess(false);
    $.post(baseEndpoint + "/saveadmin", { user: self.config.www_username(), pass: self.config.www_password() }, function (data) {
      self.saveAdminSuccess(true);
    }).fail(function () {
      alert("Failed to save Admin config");
    }).always(function () {
      self.saveAdminFetching(false);
    });
  };

  // -----------------------------------------------------------------------
  // Event: Emoncms save
  // -----------------------------------------------------------------------
  self.saveEmonCmsFetching = ko.observable(false);
  self.saveEmonCmsSuccess = ko.observable(false);
  self.saveEmonCms = function () {
    var emoncms = {
      server: self.config.emoncms_server(),
      apikey: self.config.emoncms_apikey(),
      node: self.config.emoncms_node(),
      fingerprint: self.config.emoncms_fingerprint()
    };

    if (emoncms.server === "" || emoncms.node === "") {
      alert("Please enter Emoncms server and node");
    } else if (emoncms.apikey.length != 32) {
      alert("Please enter valid Emoncms apikey");
    } else if (emoncms.fingerprint !== "" && emoncms.fingerprint.length != 59) {
      alert("Please enter valid SSL SHA-1 fingerprint");
    } else {
      self.saveEmonCmsFetching(true);
      self.saveEmonCmsSuccess(false);
      $.post(baseEndpoint + "/saveemoncms", emoncms, function (data) {
        self.saveEmonCmsSuccess(true);
      }).fail(function () {
        alert("Failed to save Admin config");
      }).always(function () {
        self.saveEmonCmsFetching(false);
      });
    }
  };

  // -----------------------------------------------------------------------
  // Event: MQTT save
  // -----------------------------------------------------------------------
  self.saveMqttFetching = ko.observable(false);
  self.saveMqttSuccess = ko.observable(false);
  self.saveMqtt = function () {
    var mqtt = {
      server: self.config.mqtt_server(),
      topic: self.config.mqtt_topic(),
      user: self.config.mqtt_user(),
      pass: self.config.mqtt_pass(),
      solar: self.config.mqtt_solar(),
      grid_ie: self.config.mqtt_grid_ie()
    };

    if (mqtt.server === "") {
      alert("Please enter MQTT server");
    } else {
      self.saveMqttFetching(true);
      self.saveMqttSuccess(false);
      $.post(baseEndpoint + "/savemqtt", mqtt, function (data) {
        self.saveMqttSuccess(true);
      }).fail(function () {
        alert("Failed to save MQTT config");
      }).always(function () {
        self.saveMqttFetching(false);
      });
    }
  };

  // -----------------------------------------------------------------------
  // Event: Save Ohm Connect Key
  // -----------------------------------------------------------------------
  self.saveOhmKeyFetching = ko.observable(false);
  self.saveOhmKeySuccess = ko.observable(false);
  self.saveOhmKey = function () {
    self.saveOhmKeyFetching(true);
    self.saveOhmKeySuccess(false);
    $.post(baseEndpoint + "/saveohmkey", { ohm: self.config.ohmkey() }, function (data) {
      self.saveOhmKeySuccess(true);
    }).fail(function () {
      alert("Failed to save Ohm key config");
    }).always(function () {
      self.saveOhmKeyFetching(false);
    });
  };

  // -----------------------------------------------------------------------
  // Event: Change divertmode (solar PV divert)
  // -----------------------------------------------------------------------
  self.changeDivertModeFetching = ko.observable(false);
  self.changeDivertModeSuccess = ko.observable(false);
  self.changeDivertMode = function(divertmode) {
    if(0 !== divertmode) {
      self.config.divertmode(divertmode);
      self.changeDivertModeFetching(true);
      self.changeDivertModeSuccess(false);
      $.post(baseEndpoint + "/divertmode", { divertmode: divertmode }, function (data) {
        self.changeDivertModeSuccess(true);
      }).fail(function () {
        alert("Failed to set divert mode");
      }).always(function () {
        self.changeDivertModeFetching(false);
      });
    }
  };

  self.divertmode = ko.pureComputed(function () {
    if('' === self.config.mqtt_solar() &&
        '' === self.config.mqtt_grid_ie())
    {
      return 0;
    } else {
      return self.config.divertmode();
    }
  });
}

$(function () {
  // Activates knockout.js
  var openevse = new OpenEvseViewModel();
  ko.applyBindings(openevse);
  openevse.start();
});

// -----------------------------------------------------------------------
// Event: Turn off Access Point
// -----------------------------------------------------------------------
document.getElementById("apoff").addEventListener("click", function (e) {

  var r = new XMLHttpRequest();
  r.open("POST", "apoff", true);
  r.onreadystatechange = function () {
    if (r.readyState != 4 || r.status != 200)
      return;
    var str = r.responseText;
    console.log(str);
    document.getElementById("apoff").style.display = 'none';
    if (ipaddress !== "")
      window.location = "http://" + ipaddress;
  };
  r.send();
});

// -----------------------------------------------------------------------
// Event: Reset config and reboot
// -----------------------------------------------------------------------
document.getElementById("reset").addEventListener("click", function (e) {

  if (confirm("CAUTION: Do you really want to Factory Reset? All setting and config will be lost.")) {
    var r = new XMLHttpRequest();
    r.open("POST", "reset", true);
    r.onreadystatechange = function () {
      if (r.readyState != 4 || r.status != 200)
        return;
      var str = r.responseText;
      console.log(str);
      if (str !== 0)
        document.getElementById("reset").innerHTML = "Resetting...";
    };
    r.send();
  }
});

// -----------------------------------------------------------------------
// Event: Restart
// -----------------------------------------------------------------------
document.getElementById("restart").addEventListener("click", function (e) {

  if (confirm("Restart emonESP? Current config will be saved, takes approximately 10s.")) {
    var r = new XMLHttpRequest();
    r.open("POST", "restart", true);
    r.onreadystatechange = function () {
      if (r.readyState != 4 || r.status != 200)
        return;
      var str = r.responseText;
      console.log(str);
      if (str !== 0)
        document.getElementById("reset").innerHTML = "Restarting";
    };
    r.send();
  }
});
