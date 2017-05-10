// Work out the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

//var baseHost = window.location.hostname;
var baseHost = 'openevse.local';
//var baseHost = '192.168.4.1';
var baseEndpoint = 'http://' + baseHost;

var statusupdate = false;
var selected_network_ssid = "";
var lastmode = "";
var ipaddress = "";

// Convert string to number, divide by scale, return result
// as a string with specified precision
function scaleString(string, scale, precision) {
  var tmpval = parseInt(string) / scale;
  return tmpval.toFixed(precision);
}

function BaseViewModel(defaults, remoteUrl, mappings = {}) {
  var self = this;
  self.remoteUrl = remoteUrl;

  // Observable promerties
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
    "free_heap": "",
    "version": "0.0.0"
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
    "timelimit": ""
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
}
RapiViewModel.prototype = Object.create(BaseViewModel.prototype);
RapiViewModel.prototype.constructor = RapiViewModel;

function OpenEvseViewModel() {
  var self = this;

  self.config = new ConfigViewModel();
  self.status = new StatusViewModel();
  self.rapi = new RapiViewModel();

  self.initialised = ko.observable(false);
  self.updating = ko.observable(false);

  var updateTimer = null;
  var updateTime = 1 * 1000;

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
      pass: self.config.mqtt_pass()
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
});


/*
// get statup status and populate input fields
var r1 = new XMLHttpRequest();
r1.open("GET", "status", false);
r1.onreadystatechange = function () {
  if (r1.readyState != 4 || r1.status != 200)
    return;
  var status = JSON.parse(r1.responseText);

  document.getElementById("passkey").value = status.pass;

   if (status.www_user!==0){
    document.getElementById("www_user").value = status.www_username;
  }

  if (status.emoncms_server!==0){
    // document.getElementById("emoncms_apikey").value = status.emoncms_apikey;
    document.getElementById("emoncms_server").value = status.emoncms_server;
    document.getElementById("emoncms_node").value = status.emoncms_node;
    document.getElementById("emoncms_fingerprint").value = status.emoncms_fingerprint;
  }

  if (status.emoncms_connected == "1"){
   document.getElementById("emoncms_connected").innerHTML = "Yes";
   if  ((status.packets_success!="undefined") & (status.packets_sent!="undefined")){
     document.getElementById("psuccess").innerHTML = "Successful posts: " + status.packets_success + " / " + status.packets_sent;
   }
  } else {
    document.getElementById("emoncms_connected").innerHTML = "No";
  }

  if (status.mqtt_server!==0){
    document.getElementById("mqtt_server").value = status.mqtt_server;
    document.getElementById("mqtt_topic").value = status.mqtt_topic;
    if (status.mqtt_user!==0){
      document.getElementById("mqtt_user").value = status.mqtt_user;
      // document.getElementById("mqtt_pass").value = status.mqtt_pass;
    }
  }

  if (status.mqtt_connected == "1"){
   document.getElementById("mqtt_connected").innerHTML = "Yes";
  } else {
    document.getElementById("mqtt_connected").innerHTML = "No";
  }

  document.getElementById("version").innerHTML = status.version;
  document.getElementById("ohmkey").value = status.ohmkey;


  if (status.mode=="AP") {
      document.getElementById("mode").innerHTML = "Access Point (AP)";
      document.getElementById("client-view").style.display = 'none';
      document.getElementById("ap-view").style.display = '';

      var out = "";
      for (var z in status.networks) {
      if (status.rssi[z]=="undefined")
        status.rssi[z]="";
        out += "<tr><td><input class='networkcheckbox' name='"+status.networks[z]+"' type='checkbox'></td><td>"+status.networks[z]+"</td><td>"+status.rssi[z]+"</td></tr>";
      }
      document.getElementById("networks").innerHTML = out;
  } else {
      if (status.mode=="STA+AP") {
          document.getElementById("mode").innerHTML = "Client + Access Point (STA+AP)";
          document.getElementById("apoff").style.display = '';
      }
    if (status.mode=="STA")
      document.getElementById("mode").innerHTML = "Client (STA)";

  	  out = "";
      out += "<tr><td>"+status.ssid+"</td><td>"+status.srssi+"</td></tr>";
      document.getElementById("sta-ssid").innerHTML = out;
      document.getElementById("sta-ip").innerHTML = "<a href='http://"+status.ipaddress+"'>"+status.ipaddress+"</a>";
      document.getElementById("ap-view").style.display = 'none';
      document.getElementById("client-view").style.display = '';
      ipaddress = status.ipaddress;
    }
};
r1.send();
*/

/*
var r2 = new XMLHttpRequest();
r2.open("GET", "config", true);
r2.timeout = 2000;
  r2.onreadystatechange = function () {
    if (r2.readyState != 4 || r2.status != 200)
      return;
    var config = JSON.parse(r2.responseText);
    document.getElementById("firmware").innerHTML = config.firmware;
    document.getElementById("protocol").innerHTML = config.protocol;
    document.getElementById("espflash").innerHTML = scaleString(config.espflash, 1024, 0) + "K";
    document.getElementById("diodet").innerHTML = config.diodet;
  	if (config.diodet == "1"){
  	  document.getElementById("diodet").innerHTML = "Disabled";
  	} else {
  	  document.getElementById("diodet").innerHTML = "Enabled";
  	}
    document.getElementById("gfcit").innerHTML = config.gfcit;
  	if (config.gfcit == "1"){
  	  document.getElementById("gfcit").innerHTML = "Disabled";
  	} else {
  	document.getElementById("gfcit").innerHTML = "Enabled";
  	}
    document.getElementById("groundt").innerHTML = config.groundt;
  	if (config.groundt == "1"){
  	  document.getElementById("groundt").innerHTML = "Disabled";
  	} else {
  	document.getElementById("groundt").innerHTML = "Enabled";
  	}
    document.getElementById("relayt").innerHTML = config.relayt;
  	if (config.relayt == "1"){
  	  document.getElementById("relayt").innerHTML = "Disabled"
  	} else {
  	document.getElementById("relayt").innerHTML = "Enabled";
  	}
    document.getElementById("ventt").innerHTML = config.ventt;
  	if (config.ventt == "1"){
  	  document.getElementById("ventt").innerHTML = "Disabled"
  	} else {
  	  document.getElementById("ventt").innerHTML = "Enabled";
  	}

    document.getElementById("service").innerHTML = config.service;
	  document.getElementById("l1min").innerHTML = config.l1min;
	  document.getElementById("l1max").innerHTML = config.l1max;
	  document.getElementById("l2min").innerHTML = config.l2min;
	  document.getElementById("l2max").innerHTML = config.l2max;
	  document.getElementById("scale").innerHTML = config.scale;
	  document.getElementById("offset").innerHTML = config.offset;
	  document.getElementById("tempt").innerHTML = config.tempt;
	  if (config.tempt == "1"){
		  document.getElementById("tempt").innerHTML = "Disabled";
    } else {
		  document.getElementById("tempt").innerHTML = "Enabled";
		}
	  document.getElementById("gfcicount").innerHTML = config.gfcicount;
	  document.getElementById("nogndcount").innerHTML = config.nogndcount;
	  document.getElementById("stuckcount").innerHTML = config.stuckcount;
	  document.getElementById("kwhlimit").innerHTML = config.kwhlimit;
	  document.getElementById("timelimit").innerHTML = config.timelimit;
  };
r2.send();
*/

/*
var r3 = new XMLHttpRequest();
r3.open("GET", "rapiupdate", true);
r3.timeout = 8000;
r3.onreadystatechange = function () {
  if (r3.readyState != 4 || r3.status != 200)
    return;
  var update = JSON.parse(r3.responseText);
  document.getElementById("comm-psent").innerHTML = update.comm_sent;
  document.getElementById("comm-psuccess").innerHTML = update.comm_success;
  document.getElementById("sta-psent").innerHTML = update.packets_sent;
  document.getElementById("sta-psuccess").innerHTML = update.packets_success;
  document.getElementById("amp").innerHTML = scaleString(update.amp, 1000, 2) + " A";
  document.getElementById("estate").innerHTML = update.estate;
  document.getElementById("espfree").innerHTML = scaleString(update.espfree, 1024, 0) + "K";;
  document.getElementById("ohmhour").innerHTML = update.ohmhour;
  // Convert watt-seconds and watt-hours to kWh
  document.getElementById("wattsec").innerHTML = scaleString(update.wattsec, 3600000, 2);
  document.getElementById("watthour").innerHTML = scaleString(update.watthour, 1000, 2);
  document.getElementById("pilot").innerHTML = update.pilot;
  document.getElementById("temp1").innerHTML = scaleString(update.temp1, 10, 1) + " C";
  document.getElementById("temp2").innerHTML = scaleString(update.temp2, 10, 1) + " C";
  document.getElementById("temp3").innerHTML = scaleString(update.temp3, 10, 1) + " C";
};
r3.send();
*/

/*
update();
setInterval(update, 10000);
*/

// -----------------------------------------------------------------------
// Periodic 10s update of last data values
// -----------------------------------------------------------------------
/*
function update() {
    var r3 = new XMLHttpRequest();
    r3.open("GET", "rapiupdate", true);
    r3.timeout = 8000;
    r3.onreadystatechange = function () {
      if (r3.readyState != 4 || r3.status != 200)
        return;
      var update = JSON.parse(r3.responseText);
      document.getElementById("comm-psent").innerHTML = update.comm_sent;
      document.getElementById("comm-psuccess").innerHTML = update.comm_success;
      document.getElementById("sta-psent").innerHTML = update.packets_sent;
      document.getElementById("sta-psuccess").innerHTML = update.packets_success;
      document.getElementById("estate").innerHTML = update.estate;
      document.getElementById("espfree").innerHTML = scaleString(update.espfree, 1024, 0) + "K";;
      document.getElementById("ohmhour").innerHTML = update.ohmhour;
      // Convert watt-seconds and watt-hours to kWh
      document.getElementById("wattsec").innerHTML = scaleString(update.wattsec, 3600000, 2);
      document.getElementById("watthour").innerHTML = scaleString(update.watthour, 1000, 2);
      document.getElementById("pilot").innerHTML = update.pilot;
      document.getElementById("temp1").innerHTML = scaleString(update.temp1, 10, 1) + " C";
      document.getElementById("temp2").innerHTML = scaleString(update.temp2, 10, 1) + " C";
      document.getElementById("temp3").innerHTML = scaleString(update.temp3, 10, 1) + " C";
    };
    r3.send();

    var r2 = new XMLHttpRequest();
    r2.open("GET", "status", false);
    r2.onreadystatechange = function () {
      if (r2.readyState != 4 || r2.status != 200)
        return;
      var status = JSON.parse(r2.responseText);

      if (status.emoncms_connected == "1"){
       document.getElementById("emoncms_connected").innerHTML = "Yes";
       if  ((status.packets_success!="undefined") & (status.packets_sent!="undefined")){
         document.getElementById("psuccess").innerHTML = "Successful posts: " + status.packets_success + " / " + status.packets_sent;
       }
      } else {
        document.getElementById("emoncms_connected").innerHTML = "No";
      }

      if (status.mqtt_connected == "1"){
       document.getElementById("mqtt_connected").innerHTML = "Yes";
      } else {
       document.getElementById("mqtt_connected").innerHTML = "No";
      }

      if ((status.mode=="STA") || (status.mode=="STA+AP")){
        // Update connected network RSSI
        var out="";
        out += "<tr><td>"+status.ssid+"</td><td>"+status.srssi+"</td></tr>";
        document.getElementById("sta-ssid").innerHTML = out;
      }
    };
   r2.send();
}
*/

/*
function updateStatus() {

  // Update status on Wifi connection
  var r1 = new XMLHttpRequest();
  r1.open("GET", "status", true);
  r1.timeout = 2000;
  r1.onreadystatechange = function () {
    if (r1.readyState != 4 || r1.status != 200)
      return;
    var status = JSON.parse(r1.responseText);

    if (status.mode == "STA+AP" || status.mode == "STA") {
      // Hide waiting message
      document.getElementById("wait-view").style.display = 'none';
      // Display mode
      if (status.mode == "STA+AP") {
        document.getElementById("mode").innerHTML = "Client + Access Point (STA+AP)";
        document.getElementById("apoff").style.display = '';
      }
      if (status.mode == "STA")
        document.getElementById("mode").innerHTML = "Client (STA)";
      document.getElementById("sta-ssid").innerHTML = status.ssid;
      document.getElementById("sta-ip").innerHTML = "<a href='http://" + status.ipaddress + "'>" + status.ipaddress + "</a>";
      document.getElementById("sta-psent").innerHTML = status.packets_sent;
      document.getElementById("sta-psuccess").innerHTML = status.packets_success;

      // View display
      document.getElementById("ap-view").style.display = 'none';
      document.getElementById("client-view").style.display = '';
    }
    lastmode = status.mode;
  };
  r1.send();
} //end update
*/

// -----------------------------------------------------------------------
// Event: WiFi Connect
// -----------------------------------------------------------------------
/*
document.getElementById("connect").addEventListener("click", function(e) {

    var passkey = document.getElementById("passkey").value;
    if (selected_network_ssid==="") {
        alert("Please select network");
    } else {
        document.getElementById("ap-view").style.display = 'none';
        document.getElementById("wait-view").style.display = '';

        var r = new XMLHttpRequest();
        r.open("POST", "savenetwork", false);
        r.setRequestHeader("Content-type","application/x-www-form-urlencoded");
        r.onreadystatechange = function () {
      if (r.readyState != 4 || r.status != 200)
        return;
	        var str = r.responseText;
	        console.log(str);
	        document.getElementById("connect").innerHTML = "Connecting...please wait 10s";

	        statusupdate = setInterval(updateStatus,5000);
        };
        r.send("ssid="+selected_network_ssid+"&pass="+passkey);
    }
});
*/

// -----------------------------------------------------------------------
// Event: Emoncms save
// -----------------------------------------------------------------------
/*
document.getElementById("save-emoncms").addEventListener("click", function (e) {

  var emoncms = {
    server: document.getElementById("emoncms_server").value,
    apikey: document.getElementById("emoncms_apikey").value,
    node: document.getElementById("emoncms_node").value,
    fingerprint: document.getElementById("emoncms_fingerprint").value
  };
  if (emoncms.server === "" || emoncms.node === "") {
    alert("Please enter Emoncms server and node");
  } else if (emoncms.apikey.length != 32) {
    alert("Please enter valid Emoncms apikey");
  } else if (emoncms.fingerprint !== "" && emoncms.fingerprint.length != 59) {
    alert("Please enter valid SSL SHA-1 fingerprint");
  } else {
    document.getElementById("save-emoncms").innerHTML = "Saving...";
    var r = new XMLHttpRequest();
    r.open("POST", "saveemnocms", true);
    r.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
    r.send("&server=" + emoncms.server + "&apikey=" + emoncms.apikey + "&node=" + emoncms.node + "&fingerprint=" + emoncms.fingerprint);
    r.onreadystatechange = function () {
      if (r.readyState != 4 || r.status != 200)
        return;
      var str = r.responseText;
      console.log(str);
      if (str !== 0)
        document.getElementById("save-emoncms").innerHTML = "Saved";
    };
  }
});
*/

// -----------------------------------------------------------------------
// Event: MQTT save
// -----------------------------------------------------------------------
/*
document.getElementById("save-mqtt").addEventListener("click", function (e) {

  var mqtt = {
    server: document.getElementById("mqtt_server").value,
    topic: document.getElementById("mqtt_topic").value,
    user: document.getElementById("mqtt_user").value,
    pass: document.getElementById("mqtt_pass").value
  };
  if (mqtt.server === "") {
    alert("Please enter MQTT server");
  } else {
    document.getElementById("save-mqtt").innerHTML = "Saving...";
    var r = new XMLHttpRequest();
    r.open("POST", "savemqtt", true);
    r.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
    r.send("&server=" + mqtt.server + "&topic=" + mqtt.topic + "&user=" + mqtt.user + "&pass=" + mqtt.pass);
    r.onreadystatechange = function () {
      console.log(mqtt);
      if (r.readyState != 4 || r.status != 200)
        return;
      var str = r.responseText;
      console.log(str);
      if (str !== 0)
        document.getElementById("save-mqtt").innerHTML = "Saved";
    };
  }
});
*/

// -----------------------------------------------------------------------
// Event: Admin save
// -----------------------------------------------------------------------
/*
document.getElementById("save-admin").addEventListener("click", function (e) {

  var admin = {
    user: document.getElementById("www_user").value,
    pass: document.getElementById("www_pass").value
  }
  document.getElementById("save-admin").innerHTML = "Saving...";
  var r = new XMLHttpRequest();
  r.open("POST", "saveadmin", true);
  r.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
  r.send("&user=" + admin.user + "&pass=" + admin.pass);
  r.onreadystatechange = function () {
    console.log(admin);
    if (r.readyState != 4 || r.status != 200)
      return;
    var str = r.responseText;
    console.log(str);
    if (str != 0)
      document.getElementById("save-admin").innerHTML = "Saved";
  };
});
*/

// -----------------------------------------------------------------------
// Event: Save Ohm Connect Key
// -----------------------------------------------------------------------
/*document.getElementById("save-ohmkey").addEventListener("click", function (e) {

  var ohmkey = document.getElementById("ohmkey").value;
  document.getElementById("save-ohmkey").innerHTML = "Saving...";
  var r = new XMLHttpRequest();
  r.open("POST", "saveohmkey", true);
  r.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
  r.send("&ohm=" + ohmkey);
  r.onreadystatechange = function () {
    console.log(ohmkey);
    if (r.readyState != 4 || r.status != 200)
      return;
    var str = r.responseText;
    console.log(str);
    if (str != 0)
      document.getElementById("save-ohmkey").innerHTML = "Saved";
  };
});
*/

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
// UI: Network select
// -----------------------------------------------------------------------
/*
var networkcheckboxes = document.getElementsByClassName("networkcheckbox");

var networkSelect = function () {
  selected_network_ssid = this.getAttribute("name");

  for (var i = 0; i < networkcheckboxes.length; i++) {
    if (networkcheckboxes[i].getAttribute("name") != selected_network_ssid)
      networkcheckboxes[i].checked = 0;
  }
};

for (var i = 0; i < networkcheckboxes.length; i++) {
  networkcheckboxes[i].addEventListener('click', networkSelect, false);
}
*/

// -----------------------------------------------------------------------
// Event:Check for updates & display current / latest
// URL /firmware
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

// -----------------------------------------------------------------------
// Event:Upload Firmware
// -----------------------------------------------------------------------
//document.getElementById("upload").addEventListener("click", function(e) {
//  window.location.href='/upload'
//});
