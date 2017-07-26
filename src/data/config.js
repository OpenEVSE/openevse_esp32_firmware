/* global $, ko, OpenEVSE */

(function() {
  "use strict";

// Configure the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
//var baseHost = "openevse.local";
//var baseHost = "192.168.4.1";
//var baseHost = "172.16.0.60";

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
  $.get(self.remoteUrl(), function (data) {
    ko.mapping.fromJS(data, self);
  }, "json").always(function () {
    self.fetching(false);
    after();
  });
};

function StatusViewModel(baseEndpoint) {
  var self = this;
  var endpoint = ko.pureComputed(function () { return baseEndpoint() + "/status"; });

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
  }, endpoint);

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

function WiFiScanViewModel(baseEndpoint)
{
  var self = this;
  var endpoint = ko.pureComputed(function () { return baseEndpoint() + "/scan"; });

  self.results = ko.mapping.fromJS([], {
    key: function(data) {
      return ko.utils.unwrapObservable(data.bssid);
    },
    create: function (options) {
      return new WiFiScanResultViewModel(options.data);
    }
  });

  // Observable properties
  self.fetching = ko.observable(false);

  self.update = function (after = function () { }) {
    self.fetching(true);
    $.get(endpoint(), function (data) {
      ko.mapping.fromJS(data, self.results);
      self.results.sort(function (left, right) {
        if(left.ssid() === right.ssid()) {
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

function ConfigViewModel(baseEndpoint) {
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
  }, endpoint);
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;

function RapiViewModel(baseEndpoint) {
  var self = this;

  self.baseEndpoint = baseEndpoint;
  var endpoint = ko.pureComputed(function () { return baseEndpoint() + "/rapiupdate"; });

  BaseViewModel.call(this, {
    "comm_sent": "0",
    "comm_success": "0",
    "amp": "0",
    "pilot": "0",
    "temp1": "0",
    "temp2": "0",
    "temp3": "0",
    "state": -1,
    "wattsec": "0",
    "watthour": "0"
  }, endpoint);

  this.rapiSend = ko.observable(false);
  this.cmd = ko.observable("");
  this.ret = ko.observable("");

  this.estate = ko.pureComputed(function () {
    var estate;
    switch (self.state()) {
      case 1:
        estate = "Not Connected";
        break;
      case 2:
        estate = "EV Connected";
        break;
      case 3:
        estate = "Charging";
        break;
      case 4:
        estate = "Vent Required";
        break;
      case 5:
        estate = "Diode Check Failed";
        break;
      case 6:
        estate = "GFCI Fault";
        break;
      case 7:
        estate = "No Earth Ground";
        break;
      case 8:
        estate = "Stuck Relay";
        break;
      case 9:
        estate = "GFCI Self Test Failed";
        break;
      case 10:
        estate = "Over Temperature";
        break;
      case 254:
        estate = "Sleeping";
        break;
      case 255:
        estate = "Disabled";
        break;
      default:
        estate = "Invalid";
        break;
    }
    return estate;
  });
}
RapiViewModel.prototype = Object.create(BaseViewModel.prototype);
RapiViewModel.prototype.constructor = RapiViewModel;
RapiViewModel.prototype.send = function() {
  var self = this;
  self.rapiSend(true);
  $.get(self.baseEndpoint() + "/r?json=1&rapi="+encodeURI(self.cmd()), function (data) {
    self.ret(">"+data.ret);
    self.cmd(data.cmd);
  }, "json").always(function () {
    self.rapiSend(false);
  });
};


function TimeViewModel(openevse)
{
  var self = this;

  function addZero(val) {
    return (val < 10 ? "0" : "") + val;
  }
  function startTimeUpdate() {
    timeUpdateTimeout = setInterval(function () {
      self.nowTimedate(new Date(self.evseTimedate().getTime() + ((new Date()) - self.localTimedate())));
    }, 1000);
  }
  function stopTimeUpdate() {
    if(null !== timeUpdateTimeout) {
      clearInterval(timeUpdateTimeout);
      timeUpdateTimeout = null;
    }
  }

  self.evseTimedate = ko.observable(new Date());
  self.localTimedate = ko.observable(new Date());
  self.nowTimedate = ko.observable(null);
  self.date = ko.pureComputed({
    read: function () {
      if(null === self.nowTimedate()) {
        return "";
      }

      return self.nowTimedate().toISOString().split("T")[0];
    },
    write: function (val) {
      self.evseTimedate(new Date(val));
      self.localTimedate(new Date());
    }});
  self.time = ko.pureComputed({
    read: function () {
      if(null === self.nowTimedate()) {
        return "--:--";
      }
      var dt = self.nowTimedate();
      return addZero(dt.getHours())+":"+addZero(dt.getMinutes())+":"+addZero(dt.getSeconds());
    },
    write: function (val) {
      var parts = val.split(":");
      var date = self.evseTimedate();
      date.setHours(parseInt(parts[0]));
      date.setMinutes(parseInt(parts[1]));
      self.evseTimedate(date);
      self.localTimedate(new Date());
    }});
  var timeUpdateTimeout = null;
  self.automaticTime = ko.observable(true);
  self.automaticTime.subscribe(function (val) {
    if(val) {
      startTimeUpdate();
    } else {
      stopTimeUpdate();
    }
  });
  self.setTime = function () {
    var newTime = self.automaticTime() ? new Date() : self.evseTimedate();
    // IMPROVE: set a few times and work out an average transmission delay, PID loop?
    openevse.time(self.timeUpdate, newTime);
  };

  self.timeUpdate = function (date) {
    stopTimeUpdate();
    self.evseTimedate(date);
    self.nowTimedate(date);
    self.localTimedate(new Date());
    if(self.automaticTime()) {
      startTimeUpdate();
    }
  };
}

function OpenEvseViewModel(baseEndpoint, rapiViewModel) {
  var self = this;
  var endpoint = ko.pureComputed(function () { return baseEndpoint() + "/r"; });
  self.openevse = new OpenEVSE(endpoint());
  endpoint.subscribe(function (end) {
    self.openevse.setEndpoint(end);
  });
  self.rapi = rapiViewModel;
  self.time = new TimeViewModel(self.openevse);

  // Option lists
  self.serviceLevels = [
    { name: "Auto", value: 0 },
    { name: "1", value: 1 },
    { name: "2", value: 2 }];
  self.currentLevels = ko.observableArray([]);
  self.timeLimits = [
    { name: "off", value: 0 },
    { name: "15 min", value: 15 },
    { name: "30 min", value: 30 },
    { name: "45 min", value: 45 },
    { name: "1 hour", value: 60 },
    { name: "1.5 hours", value: 1.5 * 60 },
    { name: "2 hours", value: 2 * 60 },
    { name: "2.5 hours", value: 2.5 * 60 },
    { name: "3 hours", value: 3 * 60 },
    { name: "4 hours", value: 4 * 60 },
    { name: "5 hours", value: 5 * 60 },
    { name: "6 hours", value: 6 * 60 },
    { name: "7 hours", value: 7 * 60 },
    { name: "8 hours", value: 8 * 60 }];

  self.serviceLevel = ko.observable(-1);
  self.actualServiceLevel = ko.observable(-1);
  self.minCurrentLevel = ko.observable(-1);
  self.maxCurrentLevel = ko.observable(-1);
  self.currentCapacity = ko.observable(-1);
  self.timeLimit = ko.observable(-1);
  self.chargeLimit = ko.observable(-1);
  self.delayTimerEnabled = ko.observable(false);
  self.delayTimerStart = ko.observable("--:--");
  self.delayTimerStop = ko.observable("--:--");
  self.gfiSelfTestEnabled = ko.observable(false);
  self.groundCheckEnabled = ko.observable(false);
  self.stuckRelayEnabled = ko.observable(false);
  self.tempCheckEnabled = ko.observable(false);
  self.diodeCheckEnabled = ko.observable(false);
  self.ventRequiredEnabled = ko.observable(false);

  // Derived states
  self.isConnected = ko.pureComputed(function () {
    return [2, 3].indexOf(self.rapi.state()) !== -1;
  });

  self.isCharging = ko.pureComputed(function () {
    return 3 === self.rapi.state();
  });

  self.isError = ko.pureComputed(function () {
    return [4, 5, 6, 7, 8, 9, 10].indexOf(self.rapi.state()) !== -1;
  });

  self.isEnabled = ko.pureComputed(function () {
    return [1, 2, 3].indexOf(self.rapi.state()) !== -1;
  });

  self.isSleeping = ko.pureComputed(function () {
    return 254 === self.rapi.state();
  });

  self.isDisabled = ko.pureComputed(function () {
    return 255 === self.rapi.state();
  });

  // helper to select an appropriate value for time limit
  self.selectTimeLimit = function(limit)
  {
    if(self.timeLimit() === limit) {
      return;
    }

    for(var i = 0; i < self.timeLimits.length; i++) {
      var time = self.timeLimits[i];
      if(time.value >= limit) {
        self.timeLimit(time.value);
        break;
      }
    }
  };

  // List of items to update on calling update(). The list will be processed one item at
  // a time.
  var updateList = [
    function () { return self.openevse.time(self.time.timeUpdate); },
    function () { return self.openevse.service_level(function (level, actual) {
      self.serviceLevel(level);
      self.actualServiceLevel(actual);
    }); },
    function () { return self.updateCurrentCapacity(); },
    function () { return self.openevse.current_capacity(function (capacity) {
      self.currentCapacity(capacity);
    }); },
    function () { return self.openevse.time_limit(function (limit) {
      self.selectTimeLimit(limit);
    }); },
    function () { return self.openevse.charge_limit(function (limit) {
      self.chargeLimit(limit);
    }); },
    function () { return self.openevse.gfi_self_test(function (enabled) {
      self.gfiSelfTestEnabled(enabled);
    }); },
    function () { return self.openevse.ground_check(function (enabled) {
      self.groundCheckEnabled(enabled);
    }); },
    function () { return self.openevse.stuck_relay_check(function (enabled) {
      self.stuckRelayEnabled(enabled);
    }); },
    function () { return self.openevse.temp_check(function (enabled) {
      self.tempCheckEnabled(enabled);
    }); },
    function () { return self.openevse.diode_check(function (enabled) {
      self.diodeCheckEnabled(enabled);
    }); },
    function () { return self.openevse.vent_required(function (enabled) {
      self.ventRequiredEnabled(enabled);
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
  self.updatingTimeLimit = ko.observable(false);
  self.updatingChargeLimit = ko.observable(false);
  self.updatingDelayTimer = ko.observable(false);
  self.updatingStatus = ko.observable(false);
  self.updatingGfiSelfTestEnabled = ko.observable(false);
  self.updatingGroundCheckEnabled = ko.observable(false);
  self.updatingStuckRelayEnabled = ko.observable(false);
  self.updatingTempCheckEnabled = ko.observable(false);
  self.updatingDiodeCheckEnabled = ko.observable(false);
  self.updatingVentRequiredEnabled = ko.observable(false);
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
        });
      }, val).always(function() {
        self.updatingServiceLevel(false);
      });
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
      }, val).always(function() {
        self.updatingCurrentCapacity(false);
      });
    });

    // Updates to the time limit
    self.timeLimit.subscribe(function (val) {
      self.updatingTimeLimit(true);
      self.openevse.time_limit(function (limit) {
        if(val !== limit) {
          self.selectTimeLimit(limit);
        }
      }, val).always(function() {
        self.updatingTimeLimit(false);
      });
    });

    // Updates to the charge limit
    self.chargeLimit.subscribe(function (val) {
      self.updatingChargeLimit(true);
      self.openevse.charge_limit(function (limit) {
        if(val !== limit) {
          self.chargeLimit(limit);
        }
      }, val).always(function() {
        self.updatingChargeLimit(false);
      });
    });

    // Updates to the GFI self test
    self.gfiSelfTestEnabled.subscribe(function (val) {
      self.updatingGfiSelfTestEnabled(true);
      self.openevse.gfi_self_test(function (enabled) {
        if(val !== enabled) {
          self.gfiSelfTestEnabled(enabled);
        }
      }, val).always(function() {
        self.updatingGfiSelfTestEnabled(false);
      });
    });

    // Updates to the ground check
    self.groundCheckEnabled.subscribe(function (val) {
      self.updatingGroundCheckEnabled(true);
      self.openevse.ground_check(function (enabled) {
        if(val !== enabled) {
          self.groundCheckEnabled(enabled);
        }
      }, val).always(function() {
        self.updatingGroundCheckEnabled(false);
      });
    });

    // Updates to the stuck relay check
    self.stuckRelayEnabled.subscribe(function (val) {
      self.updatingStuckRelayEnabled(true);
      self.openevse.stuck_relay_check(function (enabled) {
        if(val !== enabled) {
          self.stuckRelayEnabled(enabled);
        }
      }, val).always(function() {
        self.updatingStuckRelayEnabled(false);
      });
    });

    // Updates to the temp check
    self.tempCheckEnabled.subscribe(function (val) {
      self.updatingTempCheckEnabled(true);
      self.openevse.temp_check(function (enabled) {
        if(val !== enabled) {
          self.tempCheckEnabled(enabled);
        }
      }, val).always(function() {
        self.updatingTempCheckEnabled(false);
      });
    });

    // Updates to the diode check
    self.diodeCheckEnabled.subscribe(function (val) {
      self.updatingDiodeCheckEnabled(true);
      self.openevse.diode_check(function (enabled) {
        if(val !== enabled) {
          self.diodeCheckEnabled(enabled);
        }
      }, val).always(function() {
        self.updatingDiodeCheckEnabled(false);
      });
    });

    // Updates to the vent required
    self.ventRequiredEnabled.subscribe(function (val) {
      self.updatingVentRequiredEnabled(true);
      self.openevse.vent_required(function (enabled) {
        if(val !== enabled) {
          self.ventRequiredEnabled(enabled);
        }
      }, val).always(function() {
        self.updatingVentRequiredEnabled(false);
      });
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

  // delay timer logic
  function isTime(val) {
    var timeRegex = /([01]\d|2[0-3]):([0-5]\d)/;
    return timeRegex.test(val);
  }
  self.delayTimerValid = ko.pureComputed(function () {
    return isTime(self.delayTimerStart()) && isTime(self.delayTimerStop());
  });
  self.startDelayTimer = function () {
    self.updatingDelayTimer(true);
    self.openevse.timer(function () {
      self.delayTimerEnabled(true);
    }, self.delayTimerStart(), self.delayTimerStop()).always(function() {
      self.updatingDelayTimer(false);
    });
  };
  self.stopDelayTimer = function () {
    self.updatingDelayTimer(true);
    self.openevse.cancelTimer(function () {
      self.delayTimerEnabled(false);
    }).always(function() {
      self.updatingDelayTimer(false);
    });
  };

  // support for changing status
  self.setStatus = function (action) {
    self.updatingStatus(true);
    self.openevse.status(function (state) {
      self.rapi.state(state);
    }, action).always(function() {
      self.updatingStatus(false);
    });
  };

  // Support for restarting the OpenEVSE
  self.restartFetching = ko.observable(false);
  self.restart = function() {
    if (confirm("Restart OpenEVSE? Current config will be saved, takes approximately 10s.")) {
      self.restartFetching(true);
      self.openevse.reset().always(function () {
        self.restartFetching(false);
      });
    }
  };

}

function OpenEvseWiFiViewModel() {
  var self = this;

  self.baseHost = ko.observable("" !== baseHost ? baseHost : "openevse.local");
  self.baseEndpoint = ko.pureComputed(function () { return "http://" + self.baseHost(); });

  self.config = new ConfigViewModel(self.baseEndpoint);
  self.status = new StatusViewModel(self.baseEndpoint);
  self.rapi = new RapiViewModel(self.baseEndpoint);
  self.scan = new WiFiScanViewModel(self.baseEndpoint);
  self.openevse = new OpenEvseViewModel(self.baseEndpoint, self.rapi);

  self.initialised = ko.observable(false);
  self.updating = ko.observable(false);
  self.scanUpdating = ko.observable(false);

  self.bssid = ko.observable("");
  self.bssid.subscribe(function (bssid) {
    for(var i = 0; i < self.scan.results().length; i++) {
      var net = self.scan.results()[i];
      if(bssid === net.bssid()) {
        self.config.ssid(net.ssid());
        return;
      }
    }
  });

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
    self.status.update(function () {
      //if(self.baseHost().endsWith(".local")) {
      //  self.baseHost(self.status.ipaddress());
      //}
      self.config.update(function () {
        self.rapi.update(function () {
          self.openevse.update(function () {
            self.initialised(true);
            updateTimer = setTimeout(self.update, updateTime);
            self.upgradeUrl(self.baseEndpoint() + "/update");
            self.updating(false);
          });
        });
      });
      self.connect();
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
      $.post(self.baseEndpoint() + "/savenetwork", { ssid: self.config.ssid(), pass: self.config.pass() }, function () {
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
    $.post(self.baseEndpoint() + "/saveadmin", { user: self.config.www_username(), pass: self.config.www_password() }, function () {
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
    } else if (emoncms.enable && emoncms.fingerprint !== "" && emoncms.fingerprint.length !== 59) {
      alert("Please enter valid SSL SHA-1 fingerprint");
    } else {
      self.saveEmonCmsFetching(true);
      self.saveEmonCmsSuccess(false);
      $.post(self.baseEndpoint() + "/saveemoncms", emoncms, function () {
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
      $.post(self.baseEndpoint() + "/savemqtt", mqtt, function () {
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
    $.post(self.baseEndpoint() + "/saveohmkey", {
      enable: self.config.ohm_enabled(),
      ohm: self.config.ohmkey()
    }, function () {
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
  self.turnOffAccessPoint = function () {
    self.turnOffAccessPointFetching(true);
    self.turnOffAccessPointSuccess(false);
    $.post(self.baseEndpoint() + "/apoff", {
    }, function (data) {
      console.log(data);
      if (self.status.ipaddress() !== "") {
        setTimeout(function () {
          window.location = "http://" + self.status.ipaddress();
          self.turnOffAccessPointSuccess(true);
        }, 3000);
      } else {
        self.turnOffAccessPointSuccess(true);
      }
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
      $.post(self.baseEndpoint() + "/divertmode", { divertmode: divertmode }, function () {
        self.changeDivertModeSuccess(true);
      }).fail(function () {
        alert("Failed to set divert mode");
      }).always(function () {
        self.changeDivertModeFetching(false);
      });
    }
  };

  // -----------------------------------------------------------------------
  // Event: Reset config and reboot
  // -----------------------------------------------------------------------
  self.factoryResetFetching = ko.observable(false);
  self.factoryResetSuccess = ko.observable(false);
  self.factoryReset = function() {
    if (confirm("CAUTION: Do you really want to Factory Reset? All setting and config will be lost.")) {
      self.factoryResetFetching(true);
      self.factoryResetSuccess(false);
      $.post(self.baseEndpoint() + "/reset", { }, function () {
        self.factoryResetSuccess(true);
      }).fail(function () {
        alert("Failed to Factory Reset");
      }).always(function () {
        self.factoryResetFetching(false);
      });
    }
  };


  // -----------------------------------------------------------------------
  // Event: Restart
  // -----------------------------------------------------------------------
  self.restartFetching = ko.observable(false);
  self.restartSuccess = ko.observable(false);
  self.restart = function() {
    if (confirm("Restart OpenEVSE WiFi? Current config will be saved, takes approximately 10s.")) {
      self.restartFetching(true);
      self.restartSuccess(false);
      $.post(self.baseEndpoint() + "/restart", { }, function () {
        self.restartSuccess(true);
      }).fail(function () {
        alert("Failed to restart");
      }).always(function () {
        self.restartFetching(false);
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


  // -----------------------------------------------------------------------
  // Receive events from the server
  // -----------------------------------------------------------------------
  self.socket = false;
  self.connect = function () {
    self.socket = new WebSocket("ws://"+self.baseHost()+"/ws");
    self.socket.onopen = function (ev) {
        console.log(ev);
    };
    self.socket.onclose = function (ev) {
        console.log(ev);
    };
    self.socket.onmessage = function (msg) {
        console.log(msg);
        ko.mapping.fromJSON(msg.data, self.rapi);
    };
  };
}

$(function () {
  // Activates knockout.js
  var openevse = new OpenEvseWiFiViewModel();
  ko.applyBindings(openevse);
  openevse.start();
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
