/* global ko, OpenEVSE, TimeViewModel */
/* exported OpenEvseViewModel */

function OpenEvseViewModel(baseEndpoint, statusViewModel) {
  "use strict";
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
    function () { return self.openevse.temp_check(function () {
      self.tempCheckSupported(true);
    }, self.tempCheckEnabled()).error(function () {
      self.tempCheckSupported(false);
    }); },
    function () { return self.openevse.timer(function (enabled, start, stop) {
      self.delayTimerEnabled(enabled);
      self.delayTimerStart(start);
      self.delayTimerStop(stop);
    }); },
  ];
  self.updateCount = ko.observable(0);
  self.updateTotal = ko.observable(updateList.length);

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
    self.updateCount(0);
    self.nextUpdate(after);
  };
  self.nextUpdate = function (after) {
    var updateFn = updateList[self.updateCount()];
    updateFn().always(function () {
      self.updateCount(self.updateCount() + 1);
      if(self.updateCount() < updateList.length) {
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
  self.setStatus = function (action)
  {
    var currentState = self.status.state();
    if(("disabled" === action && 255 === currentState) ||
       ("sleep" === action && 254 === currentState) ||
       ("enable" === action && currentState < 254))
    {
      // nothing to do
      return;
    }

    self.updatingStatus(true);
    if(self.delayTimerEnabled() && ("sleep" === action || "enable" === action))
    {
      // If the delay Timer is enabled we have to do a bit of hackery to work around a
      // firmware issue
      //
      // When in timer mode the RAPI cpmmands to change state might not work, but
      // Emulating a button press does toggle between sleep/enable

      self.openevse.press_button(function () {
        action = false;
      }).always(function () {
        self.openevse.status(function (state) {
          self.status.state(state);
        }, action).always(function() {
          self.updatingStatus(false);
        });
      });

      return;
    }

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
