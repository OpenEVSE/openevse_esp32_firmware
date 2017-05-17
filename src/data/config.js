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
    "mqtt_server": "",
    "mqtt_topic": "",
    "mqtt_user": "",
    "mqtt_pass": "",
    "mqtt_solar: "",
    "mqtt_grid_ie: "",
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
    "version": "0.0.0"
  }, baseEndpoint + '/config');
}
ConfigViewModel.prototype = Object.create(BaseViewModel.prototype);
ConfigViewModel.prototype.constructor = ConfigViewModel;
  

    // If MQTT solar pv topic or grid topic is not set then disable solar PV divert mode
    if ((status.mqtt_solar==="") && (status.mqtt_grid_ie==="")) {
      divertmode=0;
      set_divertmode_button(divertmode); // disable mode button if solar PV / grid topics  are not configured
    }
    else{
      // Set Solar PV divert mode button to current mode status
      divertmode = status.divertmode;
      set_divertmode_button(divertmode);
    }

  // If MQTT solar pv topic or grid topic is not set then disable solar PV divert mode
  if ((status.mqtt_solar==="") && (status.mqtt_grid_ie==="")) {
    divertmode=0;
    set_divertmode_button(divertmode); // disable mode button if solar PV / grid topics  are not configured
  }
  else{
    // Set Solar PV divert mode button to current mode status
    divertmode = status.divertmode;
    set_divertmode_button(divertmode);
  }

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
// end update

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
}

$(function () {
  // Activates knockout.js
  var openevse = new OpenEvseViewModel();
  ko.applyBindings(openevse);
  openevse.start();

  set_divertmode_button(divertmode);
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

// -----------------------------------------------------------------------
// Event: Change divertmode (solar PV divert)
// -----------------------------------------------------------------------
//document.getElementById("updatecheck").addEventListener("click", function(e) {
//    document.getElementById("firmware-version").innerHTML = "<tr><td>-</td><td>Connecting...</td></tr>";
//    var r = new XMLHttpRequest();
//    r.open("POST", "firmware", true);
//    r.onreadystatechange = function () {
//        if (r.readyState != 4 || r.status != 200) return;
//        var str = r.responseText;
//        console.log(str);
//        var firmware = JSON.parse(r.responseText);
//        document.getElementById("firmware").style.display = '';
//        document.getElementById("update").style.display = '';
//        document.getElementById("firmware-version").innerHTML = "<tr><td>"+firmware.current+"</td><td>"+firmware.latest+"</td></tr>";
//	  };
//    r.send();
//});


document.getElementById("divertmode1").addEventListener("click", function(e) {
    divertmode = 1; // Normal
    set_divertmode_button(divertmode);
    changedivertmode(divertmode);
});

document.getElementById("divertmode2").addEventListener("click", function(e) {
    divertmode = 2;     // Eco
    set_divertmode_button(divertmode);
    changedivertmode(divertmode);
});

// -----------------------------------------------------------------------
// Event:Update Firmware
// -----------------------------------------------------------------------
//document.getElementById("update").addEventListener("click", function(e) {
//    document.getElementById("update-info").innerHTML = "UPDATING..."
//    var r1 = new XMLHttpRequest();
//    r1.open("POST", "update", true);
//    r1.onreadystatechange = function () {
//        if (r1.readyState != 4 || r1.status != 200) return;
//        var str1 = r1.responseText;
//        document.getElementById("update-info").innerHTML = str1
//        console.log(str1);
//	  };
//    r1.send();
//});


function set_divertmode_button(divertmode){
  // Set formatting solar PV divert divertmode button
  if (divertmode === 0){
    //DISABLE BUTTONS
    document.getElementById("divertmode1").disabled = true;
    document.getElementById("divertmode2").disabled = true;
    document.getElementById("divertmsg").innerHTML = "Error: MQTT config not configured with Solar PV / Grid topic.";
    document.getElementById("solar-wrapper").style.opacity = "0.5";
  } else {
    document.getElementById("divertmode1").disabled = false;
    document.getElementById("divertmode2").disabled = false;
    document.getElementById("divertmsg").innerHTML = "";
    document.getElementById("solar-wrapper").style.opacity = "1.0";
  }

  if (divertmode == 1){
    document.getElementById("divertmode1").style.color = '#000000';
    document.getElementById("divertmode2").style.color = 'white';
    document.getElementById("divertmode1").style.backgroundColor = '#f1f1f1';
    document.getElementById("divertmode2").style.backgroundColor = '#008080';
  }
  if (divertmode == 2){
    document.getElementById("divertmode1").style.color = 'white';
    document.getElementById("divertmode2").style.color = '#000000';
    document.getElementById("divertmode2").style.backgroundColor = '#f1f1f1';
    document.getElementById("divertmode1").style.backgroundColor = '#008080';
  }
}

function changedivertmode(divertmode){
   var r = new XMLHttpRequest();
    r.open("POST", "divertmode", true);
    r.setRequestHeader("Content-type","application/x-www-form-urlencoded");
    r.send("&divertmode="+divertmode);
    r.onreadystatechange = function () {
      console.log(divertmode);
      if (r.readyState != 4 || r.status != 200) return;
      var str = r.responseText;
      console.log(str);
    }
}

// -----------------------------------------------------------------------
// Event: Authentication save
// -----------------------------------------------------------------------
//document.getElementById("upload").addEventListener("click", function(e) {
//  window.location.href='/upload'
//});
