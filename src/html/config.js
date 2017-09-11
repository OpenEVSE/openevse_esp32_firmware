/* global $, ko, OpenEVSE */

(function() {
  "use strict";

// Configure the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
//var baseHost = "openevse.local";
//var baseHost = "192.168.4.1";
//var baseHost = "172.16.0.70";

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
    "packets_sent": 0,
    "packets_success": 0,
    "emoncms_connected": 0,
    "mqtt_connected": 0,
    "ohm_hour": "",
    "free_heap": 0,
    "comm_sent": 0,
    "comm_success": 0,
    "amp": 0,
    "pilot": 0,
    "temp1": 0,
    "temp2": 0,
    "temp3": 0,
    "state": 0,
    "elapsed": 0,
    "wattsec": 0,
    "watthour": 0,
    "divertmode": 1
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


  this.estate = ko.pureComputed(function () {
    var estate;
    switch (self.state()) {
      case 0:
        estate = "Starting";
        break;
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
        estate = "Waiting";
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
StatusViewModel.prototype = Object.create(BaseViewModel.prototype);
StatusViewModel.prototype.constructor = StatusViewModel;


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
    "espflash": 0,
    "diodet": 0,
    "gfcit": 0,
    "groundt": 0,
    "relayt": 0,
    "ventt": 0,
    "tempt": 0,
    "scale": 1,
    "offset": 0,
    "gfcicount": 0,
    "nogndcount": 0,
    "stuckcount": 0,
    "version": "0.0.0"
  }, endpoint);
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;

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

function RapiViewModel(baseEndpoint) {
  var self = this;

  self.baseEndpoint = baseEndpoint;

  self.rapiSend = ko.observable(false);
  self.cmd = ko.observable("");
  self.ret = ko.observable("");

  self.send = function() {
    self.rapiSend(true);
    $.get(self.baseEndpoint() + "/r?json=1&rapi="+encodeURI(self.cmd()), function (data) {
      self.ret(">"+data.ret);
      self.cmd(data.cmd);
    }, "json").always(function () {
      self.rapiSend(false);
    });
  };
}

function TimeViewModel(openevse)
{
  var self = this;

  function addZero(val) {
    return (val < 10 ? "0" : "") + val;
  }
  function startTimeUpdate() {
    timeUpdateTimeout = setInterval(function () {
      if(self.automaticTime()) {
        self.nowTimedate(new Date(self.evseTimedate().getTime() + ((new Date()) - self.localTimedate())));
      }
      if(openevse.isCharging()) {
        self.elapsedNow(new Date((openevse.status.elapsed() * 1000) + ((new Date()) - self.elapsedLocal())));
      }
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

  self.elapsedNow = ko.observable(new Date(0));
  self.elapsedLocal = ko.observable(new Date());

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
        return "--:--:--";
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
  self.elapsed = ko.pureComputed(function () {
    if(null === self.nowTimedate()) {
      return "0:00:00";
    }
    var dt = self.elapsedNow();
    return addZero(dt.getHours())+":"+addZero(dt.getMinutes())+":"+addZero(dt.getSeconds());
  });

  openevse.status.elapsed.subscribe(function (val) {
      self.elapsedNow(new Date(val * 1000));
      self.elapsedLocal(new Date());
  });

  var timeUpdateTimeout = null;
  self.automaticTime = ko.observable(true);
  self.setTime = function () {
    var newTime = self.automaticTime() ? new Date() : self.evseTimedate();
    // IMPROVE: set a few times and work out an average transmission delay, PID loop?
    openevse.openevse.time(self.timeUpdate, newTime);
  };

  self.timeUpdate = function (date) {
    stopTimeUpdate();
    self.evseTimedate(date);
    self.nowTimedate(date);
    self.localTimedate(new Date());
    startTimeUpdate();
  };
}

function OpenEvseViewModel(baseEndpoint, statusViewModel) {
  var self = this;
  var endpoint = ko.pureComputed(function () { return baseEndpoint() + "/r"; });
  self.openevse = new OpenEVSE(endpoint());
  endpoint.subscribe(function (end) {
    self.openevse.setEndpoint(end);
  });
  self.status = statusViewModel;
  self.time = new TimeViewModel(self);

  // Option lists
  self.serviceLevels = [
    { name: "Auto", value: 0 },
    { name: "1", value: 1 },
    { name: "2", value: 2 }];
  self.currentLevels = ko.observableArray([]);
  self.timeLimits = [
    { name: "none", value: 0 },
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

  self.chargeLimits = [
    { name: "none", value: 0 },
    { name: "1 kWh", value: 1 },
    { name: "2 kWh", value: 2 },
    { name: "3 kWh", value: 3 },
    { name: "4 kWh", value: 4 },
    { name: "5 kWh", value: 5 },
    { name: "6 kWh", value: 6 },
    { name: "7 kWh", value: 7 },
    { name: "8 kWh", value: 8 },
    { name: "9 kWh", value: 9 },
    { name: "10 kWh", value: 10 },
    { name: "15 kWh", value: 11 },
    { name: "20 kWh", value: 12 },
    { name: "25 kWh", value: 25 },
    { name: "30 kWh", value: 30 },
    { name: "35 kWh", value: 35 },
    { name: "40 kWh", value: 40 },
    { name: "45 kWh", value: 45 },
    { name: "50 kWh", value: 50 },
    { name: "55 kWh", value: 55 },
    { name: "60 kWh", value: 60 },
    { name: "70 kWh", value: 70 },
    { name: "80 kWh", value: 80 },
    { name: "90 kWh", value: 90 }];

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

  // Saftey tests
  self.gfiSelfTestEnabled = ko.observable(false);
  self.groundCheckEnabled = ko.observable(false);
  self.stuckRelayEnabled = ko.observable(false);
  self.tempCheckEnabled = ko.observable(false);
  self.diodeCheckEnabled = ko.observable(false);
  self.ventRequiredEnabled = ko.observable(false);
  self.allTestsEnabled = ko.pureComputed(function () {
    return self.gfiSelfTestEnabled() &&
           self.groundCheckEnabled() &&
           self.stuckRelayEnabled() &&
           self.tempCheckEnabled() &&
           self.diodeCheckEnabled() &&
           self.ventRequiredEnabled();
    });

  self.tempCheckSupported = ko.observable(false);

  // Derived states
  self.isConnected = ko.pureComputed(function () {
    return [2, 3].indexOf(self.status.state()) !== -1;
  });

  self.isReady = ko.pureComputed(function () {
    return [0, 1].indexOf(self.status.state()) !== -1;
  });

  self.isCharging = ko.pureComputed(function () {
    return 3 === self.status.state();
  });

  self.isError = ko.pureComputed(function () {
    return [4, 5, 6, 7, 8, 9, 10].indexOf(self.status.state()) !== -1;
  });

  self.isEnabled = ko.pureComputed(function () {
    return [0, 1, 2, 3].indexOf(self.status.state()) !== -1;
  });

  self.isSleeping = ko.pureComputed(function () {
    return 254 === self.status.state();
  });

  self.isDisabled = ko.pureComputed(function () {
    return 255 === self.status.state();
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

  // helper to select an appropriate value for charge limit
  self.selectChargeLimit = function(limit)
  {
    if(self.chargeLimit() === limit) {
      return;
    }

    for(var i = 0; i < self.chargeLimits.length; i++) {
      var charge = self.chargeLimits[i];
      if(charge.value >= limit) {
        self.chargeLimit(charge.value);
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
      self.selectChargeLimit(limit);
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
    }); },
    function () { return self.openevse.over_temperature_thresholds(function () {
      self.tempCheckSupported(true);
    }).error(function () {
      self.tempCheckSupported(false);
    }); },
    function () { return self.openevse.timer(function (enabled, start, stop) {
      self.delayTimerEnabled(enabled);
      self.delayTimerStart(start);
      self.delayTimerStop(stop);
    }); },
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
  self.savedServiceLevel = ko.observable(false);
  self.updatingCurrentCapacity = ko.observable(false);
  self.savedCurrentCapacity = ko.observable(false);
  self.updatingTimeLimit = ko.observable(false);
  self.savedTimeLimit = ko.observable(false);
  self.updatingChargeLimit = ko.observable(false);
  self.savedChargeLimit = ko.observable(false);
  self.updatingDelayTimer = ko.observable(false);
  self.savedDelayTimer = ko.observable(false);
  self.updatingStatus = ko.observable(false);
  self.savedStatus = ko.observable(false);
  self.updatingGfiSelfTestEnabled = ko.observable(false);
  self.savedGfiSelfTestEnabled = ko.observable(false);
  self.updatingGroundCheckEnabled = ko.observable(false);
  self.savedGroundCheckEnabled = ko.observable(false);
  self.updatingStuckRelayEnabled = ko.observable(false);
  self.savedStuckRelayEnabled = ko.observable(false);
  self.updatingTempCheckEnabled = ko.observable(false);
  self.savedTempCheckEnabled = ko.observable(false);
  self.updatingDiodeCheckEnabled = ko.observable(false);
  self.savedDiodeCheckEnabled = ko.observable(false);
  self.updatingVentRequiredEnabled = ko.observable(false);
  self.savedVentRequiredEnabled = ko.observable(false);

  self.setForTime = function (flag, time) {
    flag(true);
    setTimeout(function () { flag(false); }, time);
  };

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
        self.setForTime(self.savedServiceLevel, 2000);
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
        self.setForTime(self.savedCurrentCapacity, 2000);
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
        self.setForTime(self.savedTimeLimit, 2000);
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
        self.setForTime(self.savedChargeLimit, 2000);
        if(val !== limit) {
          self.selectChargeLimit(limit);
        }
      }, val).always(function() {
        self.updatingChargeLimit(false);
      });
    });

    // Updates to the GFI self test
    self.gfiSelfTestEnabled.subscribe(function (val) {
      self.updatingGfiSelfTestEnabled(true);
      self.openevse.gfi_self_test(function (enabled) {
        self.setForTime(self.savedGfiSelfTestEnabled, 2000);
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
        self.setForTime(self.savedGroundCheckEnabled, 2000);
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
      self.savedStuckRelayEnabled(false);
      self.openevse.stuck_relay_check(function (enabled) {
        self.savedStuckRelayEnabled(true);
        setTimeout(function () { self.savedStuckRelayEnabled(false); }, 2000);
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
        self.setForTime(self.savedTempCheckEnabled, 2000);
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
        self.setForTime(self.savedDiodeCheckEnabled, 2000);
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
        self.setForTime(self.savedVentRequiredEnabled, 2000);
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
      self.status.state(state);
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
  self.openevse = new OpenEvseViewModel(self.baseEndpoint, self.status);

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

  // Info text display state
  self.showMqttInfo = ko.observable(false);
  self.showSolarDivert = ko.observable(false);

  self.toggle = function (flag) {
    flag(!flag());
  };

  // Advanced mode
  self.advancedMode = ko.observable(false);

  // Developer mode
  self.developerMode = ko.observable(false);
  self.developerMode.subscribe(function (val) { if(val) {
    self.advancedMode(true); // Enabling dev mode implicitly enables advanced mode
  }});

  var updateTimer = null;
  var updateTime = 5 * 1000;

  var scanTimer = null;
  var scanTime = 3 * 1000;

  // Tabs
  var tab = "status";
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

  // Upgrade URL
  self.upgradeUrl = ko.observable("about:blank");

  // -----------------------------------------------------------------------
  // Initialise the app
  // -----------------------------------------------------------------------
  self.start = function () {
    self.updating(true);
    self.status.update(function () {
      self.config.update(function () {
        // If we are accessing on a .local domain try and redirect
        if(self.baseHost().endsWith(".local") && "" !== self.status.ipaddress()) {
          if("" === self.config.www_username())
          {
            // Redirect to the IP internally
            self.baseHost(self.status.ipaddress());
          } else {
            window.location.replace("http://" + self.status.ipaddress());
          }
        }
        self.openevse.update(function () {
          self.initialised(true);
          updateTimer = setTimeout(self.update, updateTime);

          // Load the upgrade frame
          self.upgradeUrl(self.baseEndpoint() + "/update");

          // Load the images
          var imgDefer = document.getElementsByTagName("img");
          for (var i=0; i<imgDefer.length; i++) {
            if(imgDefer[i].getAttribute("data-src")) {
              imgDefer[i].setAttribute("src", imgDefer[i].getAttribute("data-src"));
            }
          }

          self.updating(false);
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
      updateTimer = setTimeout(self.update, updateTime);
      self.updating(false);
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
    } else if (emoncms.enable && emoncms.apikey.length !== 32 && emoncms.apikey !== "___DUMMY_PASSWORD___") {
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
    if(self.status.divertmode() !== divertmode) {
      self.status.divertmode(divertmode);
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

  self.isEcoModeAvailable = ko.pureComputed(function () {
    return self.config.mqtt_enabled() &&
           ("" !== self.config.mqtt_solar() ||
            "" !== self.config.mqtt_grid_ie());
  });

  self.ecoMode = ko.pureComputed({
    read: function () {
      return 2 === self.status.divertmode();
    },
    write: function(val) {
      self.changeDivertMode(val ? 2 : 1);
    }
  });


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

  // -----------------------------------------------------------------------
  // Receive events from the server
  // -----------------------------------------------------------------------
  self.pingInterval = false;
  self.socket = false;
  self.connect = function () {
    self.socket = new WebSocket("ws://"+self.baseHost()+"/ws");
    self.socket.onopen = function (ev) {
      console.log(ev);
      self.pingInterval = setInterval(function () {
        self.socket.send("{\"ping\":1}");
      }, 1000);
    };
    self.socket.onclose = function (ev) {
      console.log(ev);
      self.reconnect();
    };
    self.socket.onmessage = function (msg) {
      console.log(msg);
      ko.mapping.fromJSON(msg.data, self.status);
    };
    self.socket.onerror = function (ev) {
      console.log(ev);
      self.socket.close();
      self.reconnect();
    };
  };
  self.reconnect = function() {
    if(false !== self.pingInterval) {
      clearInterval(self.pingInterval);
      self.pingInterval = false;
    }
    setTimeout(function () {
      self.connect();
    }, 500);
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
