/* global $, ko, OpenEVSE */

(function() {
  "use strict";

// Work out the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

//var baseHost = window.location.hostname;
//var baseHost = "openevse.local";
//var baseHost = "192.168.4.1";
var baseHost = "172.16.0.54";
var baseEndpoint = "http://" + baseHost;

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
  }, "json").always(function () {
    self.fetching(false);
    after();
  });
};

function StatusViewModel() {
  var self = this;

  BaseViewModel.call(self, {
    "mode": "ERR",
    "srssi": "",
    "ipaddress": "",
    "packets_sent": "",
    "packets_success": "",
    "emoncms_connected": "",
    "mqtt_connected": "",
    "ohm_hour": "",
    "free_heap": ""
  }, baseEndpoint + "/status");

  // Some devired values
  self.isWifiClient = ko.pureComputed(function () {
    return ("STA" === self.mode()) || ("STA+AP" === self.mode());
  });
  self.isWifiAccessPoint = ko.pureComputed(function () {
    return ("AP" === self.mode()) || ("STA+AP" === self.mode());
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

function WiFiScanResultViewModel(data)
{
    var self = this;
    ko.mapping.fromJS(data, {}, self);
}

function WiFiScanViewModel()
{
  var self = this;

  self.results = ko.mapping.fromJS([], {
    key: function(data) {
      return ko.utils.unwrapObservable(data.bssid);
    },
    create: function (options) {
      return new WiFiScanResultViewModel(options.data);
    }
  });

  self.remoteUrl = baseEndpoint + "/scan";

  // Observable properties
  self.fetching = ko.observable(false);

  self.update = function (after = function () { }) {
    self.fetching(true);
    $.get(self.remoteUrl, function (data) {
      ko.mapping.fromJS(data, self.results);
      self.results.sort(function (left, right) {
        if(left.ssid() == right.ssid()) {
          return left.rssi() < right.rssi() ? 1 : -1;
        }
        return left.ssid() < right.ssid() ? -1 : 1;
      });
    }, "json").always(function () {
      self.fetching(false);
      after();
    });
  };
}

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
  }, baseEndpoint + "/config");
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
  }, baseEndpoint + "/rapiupdate");

  this.rapiSend = ko.observable(false);
  this.cmd = ko.observable("");
  this.ret = ko.observable("");
}
RapiViewModel.prototype = Object.create(BaseViewModel.prototype);
RapiViewModel.prototype.constructor = RapiViewModel;
RapiViewModel.prototype.send = function() {
  var self = this;
  self.rapiSend(true);
  $.get(baseEndpoint + "/r?json=1&rapi="+encodeURI(self.cmd()), function (data) {
    self.ret(">"+data.ret);
    self.cmd(data.cmd);
  }, "json").always(function () {
    self.rapiSend(false);
  });
};

function OpenEvseViewModel(configViewModel) {
  var self = this;
  self.openevse = new OpenEVSE(baseEndpoint + "/r");
  self.config = configViewModel;

  // Option lists
  self.serviceLevels = [
    { name: "Auto", value: 0 },
    { name: "1", value: 1 },
    { name: "2", value: 2 }];
  self.serviceLevel = ko.observable(-1);
  self.actualServiceLevel = ko.observable(-1);

  self.minCurrentLevel = ko.observable(-1);
  self.maxCurrentLevel = ko.observable(-1);
  self.currentCapacity = ko.observable(-1);
  self.currentLevels = ko.observableArray([]);

  self.timedate = ko.observable(new Date());
  self.time = ko.pureComputed(function () {
    var dt = self.timedate();
    return dt.getDate()+"/"+dt.getMonth()+"/"+dt.getFullYear();
  });
  self.date = ko.pureComputed(function () {
    var dt = self.timedate();
    return dt.getHours()+":"+dt.getMinutes()+":"+dt.getSeconds();
  });

  var updateList = [
    function () { return self.openevse.time(function (date) {
      self.timedate(date);
    }); },
    function () { return self.openevse.service_level(function (level, actual) {
      self.serviceLevel(level);
      self.actualServiceLevel(actual);
    }); },
    function () { return self.updateCurrentCapacity(); },
    function () { return self.openevse.current_capacity(function (capacity) {
      self.currentCapacity(capacity);
    }); }
  ];
  var updateCount = -1;

  self.updateCurrentCapacity = function () {
    return self.openevse.current_capacity_range(function (min, max) {
      self.minCurrentLevel(min);
      self.maxCurrentLevel(max);
      var capacity = self.currentCapacity();
      self.currentLevels.removeAll();
      for(var i = self.minCurrentLevel(); i <= self.maxCurrentLevel(); i++) {
        self.currentLevels.push({name: i+" A", value: i});
      }
      self.currentCapacity(capacity);
    });
  };

  self.updatingServiceLevel = ko.observable(false);
  self.updatingCurrentCapacity = ko.observable(false);
  /*self.updating = ko.pureComputed(function () {
    return self.updatingServiceLevel() ||
           self.updateCurrentCapacity();
  });*/

  var subscribed = false;
  self.subscribe = function ()
  {
    if(subscribed) {
      return;
    }

    // Updates to the service level
    self.serviceLevel.subscribe(function (val) {
      self.updatingServiceLevel(true);
      self.openevse.service_level(function (level, actual) {
        self.actualServiceLevel(actual);
        self.updateCurrentCapacity().always(function () {
          self.updatingServiceLevel(false);
        });
      }, val);
    });

    // Updates to the current capacity
    self.currentCapacity.subscribe(function (val) {
      if(true === self.updatingServiceLevel()) {
        return;
      }
      self.updatingCurrentCapacity(true);
      self.openevse.current_capacity(function (capacity) {
        if(val !== capacity) {
          self.currentCapacity(capacity);
        }
        self.updatingCurrentCapacity(false);
      }, val);
    });

    subscribed = true;
  };

  self.update = function (after = function () { }) {
    updateCount = 0;
    self.nextUpdate(after);
  };
  self.nextUpdate = function (after) {
    var updateFn = updateList[updateCount];
    updateFn().always(function () {
      if(++updateCount < updateList.length) {
        self.nextUpdate(after);
      } else {
        self.subscribe();
        after();
      }
    });
  };
}

function OpenEvseWiFiViewModel() {
  var self = this;

  self.config = new ConfigViewModel();
  self.status = new StatusViewModel();
  self.rapi = new RapiViewModel();
  self.scan = new WiFiScanViewModel();
  self.openevse = new OpenEvseViewModel(self.config);

  self.initialised = ko.observable(false);
  self.updating = ko.observable(false);
  self.scanUpdating = ko.observable(false);

  var updateTimer = null;
  var updateTime = 5 * 1000;

  var scanTimer = null;
  var scanTime = 3 * 1000;

  // Tabs
  var tab = "system";
  if("" !== window.location.hash) {
    tab = window.location.hash.substr(1);
  }
  self.tab = ko.observable(tab);
  self.tab.subscribe(function (val) {
    window.location.hash = "#" + val;
  });
  self.isSystem = ko.pureComputed(function() { return "system" === self.tab(); });
  self.isServices = ko.pureComputed(function() { return "services" === self.tab(); });
  self.isStatus = ko.pureComputed(function() { return "status" === self.tab(); });
  self.isRapi = ko.pureComputed(function() { return "rapi" === self.tab(); });
  self.isMode = ko.pureComputed(function() { return "mode" === self.tab(); });

  // Upgrade URL
  self.upgradeUrl = ko.observable("about:blank");

  // -----------------------------------------------------------------------
  // Initialise the app
  // -----------------------------------------------------------------------
  self.start = function () {
    self.updating(true);
    self.config.update(function () {
      self.status.update(function () {
        self.rapi.update(function () {
          self.openevse.update(function () {
            self.initialised(true);
            updateTimer = setTimeout(self.update, updateTime);
            self.upgradeUrl(baseEndpoint + "/update");
            self.updating(false);
          });
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

  // -----------------------------------------------------------------------
  // WiFi scan update
  // -----------------------------------------------------------------------
  var scanEnabled = false;
  self.startScan = function () {
    if (self.scanUpdating()) {
      return;
    }
    scanEnabled = true;
    self.scanUpdating(true);
    if (null !== scanTimer) {
      clearTimeout(scanTimer);
      scanTimer = null;
    }
    self.scan.update(function () {
      if(scanEnabled) {
        scanTimer = setTimeout(self.startScan, scanTime);
      }
      self.scanUpdating(false);
    });
  };

  self.stopScan = function() {
    scanEnabled = false;
    if (self.scanUpdating()) {
      return;
    }

    if (null !== scanTimer) {
      clearTimeout(scanTimer);
      scanTimer = null;
    }
  };

  self.wifiConnecting = ko.observable(false);
  self.status.mode.subscribe(function (newValue) {
    if(newValue === "STA+AP" || newValue === "STA") {
      self.wifiConnecting(false);
    }
    if(newValue === "STA+AP" || newValue === "AP") {
      self.startScan();
    } else {
      self.stopScan();
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
      enable: self.config.emoncms_enabled(),
      server: self.config.emoncms_server(),
      apikey: self.config.emoncms_apikey(),
      node: self.config.emoncms_node(),
      fingerprint: self.config.emoncms_fingerprint()
    };

    if (emoncms.enable && (emoncms.server === "" || emoncms.node === "")) {
      alert("Please enter Emoncms server and node");
    } else if (emoncms.enable && emoncms.apikey.length !== 32) {
      alert("Please enter valid Emoncms apikey");
    } else if (emoncms.enable && emoncms.fingerprint !== "" && emoncms.fingerprint.length != 59) {
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
      enable: self.config.mqtt_enabled(),
      server: self.config.mqtt_server(),
      topic: self.config.mqtt_topic(),
      user: self.config.mqtt_user(),
      pass: self.config.mqtt_pass(),
      solar: self.config.mqtt_solar(),
      grid_ie: self.config.mqtt_grid_ie()
    };

    if (mqtt.enable && mqtt.server === "") {
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
    $.post(baseEndpoint + "/saveohmkey", {
      enable: self.config.ohm_enabled(),
      ohm: self.config.ohmkey()
    }, function (data) {
      self.saveOhmKeySuccess(true);
    }).fail(function () {
      alert("Failed to save Ohm key config");
    }).always(function () {
      self.saveOhmKeyFetching(false);
    });
  };

  // -----------------------------------------------------------------------
  // Event: Turn off Access Point
  // -----------------------------------------------------------------------
  self.turnOffAccessPointFetching = ko.observable(false);
  self.turnOffAccessPointSuccess = ko.observable(false);
  self.turnOffAccessPoint = function (e) {
    self.turnOffAccessPointFetching(true);
    self.turnOffAccessPointSuccess(false);
    $.post(baseEndpoint + "/apoff", {
    }, function (data) {
      console.log(data);
      if (self.status.ipaddress() !== "") {
        window.location = "http://" + self.status.ipaddress();
      }
      self.turnOffAccessPointSuccess(true);
    }).fail(function () {
      alert("Failed to turn off Access Point");
    }).always(function () {
      self.turnOffAccessPointFetching(false);
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
    if(!self.config.mqtt_enabled() ||
       ("" === self.config.mqtt_solar() &&
        "" === self.config.mqtt_grid_ie()))
    {
      return 0;
    } else {
      return self.config.divertmode();
    }
  });
}

$(function () {
  // Activates knockout.js
  var openevse = new OpenEvseWiFiViewModel();
  ko.applyBindings(openevse);
  openevse.start();
});

// -----------------------------------------------------------------------
// Event: Reset config and reboot
// -----------------------------------------------------------------------
document.getElementById("reset").addEventListener("click", function (e) {

  if (confirm("CAUTION: Do you really want to Factory Reset? All setting and config will be lost.")) {
    var r = new XMLHttpRequest();
    r.open("POST", "reset", true);
    r.onreadystatechange = function () {
      if (r.readyState !== 4 || r.status !== 200) {
        return;
      }
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

})();


// Convert string to number, divide by scale, return result
// as a string with specified precision
/* exported scaleString */
function scaleString(string, scale, precision) {
  "use strict";
  var tmpval = parseInt(string) / scale;
  return tmpval.toFixed(precision);
}
