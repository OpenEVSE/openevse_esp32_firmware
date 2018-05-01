/* global $, ko, ConfigViewModel, StatusViewModel, RapiViewModel, WiFiScanViewModel, WiFiConfigViewModel, OpenEvseViewModel */
/* exported OpenEvseWiFiViewModel */

function OpenEvseWiFiViewModel(baseHost, basePort, baseProtocol)
{
  "use strict";
  var self = this;

  self.baseHost = ko.observable("" !== baseHost ? baseHost : "openevse.local");
  self.basePort = ko.observable(basePort);
  self.baseProtocol = ko.observable(baseProtocol);

  self.baseEndpoint = ko.pureComputed(function () {
    var endpoint = "//" + self.baseHost();
    if(80 !== self.basePort()) {
      endpoint += ":"+self.basePort();
    }
    return endpoint;
  });

  self.wsEndpoint = ko.pureComputed(function () {
    var endpoint = "ws://" + self.baseHost();
    if("https:" === self.baseProtocol()){
      endpoint = "wss://" + self.baseHost();
    }
    if(80 !== self.basePort()) {
      endpoint += ":"+self.basePort();
    }
    endpoint += "/ws";
    return endpoint;
  });

  self.config = new ConfigViewModel(self.baseEndpoint);
  self.status = new StatusViewModel(self.baseEndpoint);
  self.rapi = new RapiViewModel(self.baseEndpoint);
  self.scan = new WiFiScanViewModel(self.baseEndpoint);
  self.wifi = new WiFiConfigViewModel(self.baseEndpoint, self.config, self.status, self.scan);
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
  self.showSafety = ko.observable(false);


  self.toggle = function (flag) {
    flag(!flag());
  };

  // Advanced mode
  self.advancedMode = ko.observable(false);
  self.advancedMode.subscribe(function (val) {
    self.setCookie("advancedMode", val.toString());
  });

  // Developer mode
  self.developerMode = ko.observable(false);
  self.developerMode.subscribe(function (val) {
    self.setCookie("developerMode", val.toString());
    if(val) {
      self.advancedMode(true); // Enabling dev mode implicitly enables advanced mode
    }
  });

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
  self.loadedCount = ko.observable(0);
  self.itemsLoaded = ko.pureComputed(function () {
    return self.loadedCount() + self.openevse.updateCount();
  });
  self.itemsTotal = ko.observable(2 + self.openevse.updateTotal());
  self.start = function () {
    self.updating(true);
    self.status.update(function () {
      self.loadedCount(self.loadedCount() + 1);
      self.config.update(function () {
        self.loadedCount(self.loadedCount() + 1);
        // If we are accessing on a .local domain try and redirect
        if(self.baseHost().endsWith(".local") && "" !== self.status.ipaddress()) {
          if("" === self.config.www_username())
          {
            // Redirect to the IP internally
            self.baseHost(self.status.ipaddress());
          } else {
            window.location.replace("http://" + self.status.ipaddress() + ":" + self.basePort());
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

    // Set the advanced and developer modes from Cookies
    self.advancedMode(self.getCookie("advancedMode", "false") === "true");
    self.developerMode(self.getCookie("developerMode", "false") === "true");
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

  self.haveSolar =ko.pureComputed(function () {
    return "" !== self.config.mqtt_solar();
  });

  self.haveGridIe =ko.pureComputed(function () {
    return "" !== self.config.mqtt_grid_ie();
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
  self.reconnectInterval = false;
  self.socket = false;
  self.connect = function () {
    self.socket = new WebSocket(self.wsEndpoint());
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
    if(false === self.reconnectInterval) {
      self.reconnectInterval = setTimeout(function () {
        self.reconnectInterval = false;
        self.connect();
      }, 500);
    }
  };

  // Cookie management, based on https://www.w3schools.com/js/js_cookies.asp
  self.setCookie = function (cname, cvalue, exdays = false) {
    var expires = "";
    if(false !== exdays) {
      var d = new Date();
      d.setTime(d.getTime() + (exdays * 24 * 60 * 60 * 1000));
      expires = ";expires="+d.toUTCString();
    }
    document.cookie = cname + "=" + cvalue + expires + ";path=/";
  };

  self.getCookie = function (cname, def = "") {
    var name = cname + "=";
    var ca = document.cookie.split(";");
    for(var i = 0; i < ca.length; i++) {
      var c = ca[i];
      while (c.charAt(0) === " ") {
        c = c.substring(1);
      }
      if (c.indexOf(name) === 0) {
        return c.substring(name.length, c.length);
      }
    }
    return def;
  };
}
