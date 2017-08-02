/* global $, ko */

(function() {
  "use strict";

// Configure the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
//var baseHost = "openevse.local";
//var baseHost = "192.168.4.1";
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

function ConfigViewModel() {
  BaseViewModel.call(this, {
    "ssid": "",
    "pass": "",
    "firmware": "-",
    "protocol": "-",
    "espflash": "",
    "version": "0.0.0"
  }, baseEndpoint + "/config");
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;

function WiFiPortal() {
  var self = this;

  self.config = new ConfigViewModel();
  self.status = new StatusViewModel();
  self.scan = new WiFiScanViewModel();

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

  // -----------------------------------------------------------------------
  // Initialise the app
  // -----------------------------------------------------------------------
  self.start = function () {
    self.updating(true);
    self.config.update(function () {
      self.status.update(function () {
        self.initialised(true);
        updateTimer = setTimeout(self.update, updateTime);
        self.updating(false);
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
      $.post(baseEndpoint + "/savenetwork", { ssid: self.config.ssid(), pass: self.config.pass() }, function () {
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
  // Event: Turn off Access Point
  // -----------------------------------------------------------------------
  self.turnOffAccessPointFetching = ko.observable(false);
  self.turnOffAccessPointSuccess = ko.observable(false);
  self.turnOffAccessPoint = function () {
    self.turnOffAccessPointFetching(true);
    self.turnOffAccessPointSuccess(false);
    $.post(baseEndpoint + "/apoff", {
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
}

$(function () {
  // Activates knockout.js
  var openevse = new WiFiPortal();
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
