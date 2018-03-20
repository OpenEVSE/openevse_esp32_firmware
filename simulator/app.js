/* jslint node: true, esversion: 6 */
"use strict";


const express = require("express");
const path = require("path");
const bodyParser = require("body-parser");
const minimist = require("minimist");


var config = {
  "firmware": "-",
  "protocol": "-",
  "espflash": 4194304,
  "version": "DEMO",
  "diodet": 0,
  "gfcit": 0,
  "groundt": 0,
  "relayt": 0,
  "ventt": 0,
  "tempt": 0,
  "service": 2,
  "scale": 220,
  "offset": 0,
  "ssid": "demo",
  "pass": "___DUMMY_PASSWORD___",
  "emoncms_enabled": false,
  "emoncms_server": "emoncms.org",
  "emoncms_node": "openevse",
  "emoncms_apikey": "",
  "emoncms_fingerprint": "",
  "mqtt_enabled": true,
  "mqtt_server": "emonpi.local",
  "mqtt_topic": "openevse",
  "mqtt_user": "emonpi",
  "mqtt_pass": "___DUMMY_PASSWORD___",
  "mqtt_solar": "emon/emonpi/power1",
  "mqtt_grid_ie": "",
  "www_username": "",
  "www_password": "",
  "ohm_enabled": false
};

var status = {
  "mode": "STA",
  "wifi_client_connected": 1,
  "srssi": -50,
  "ipaddress": "172.16.0.191",
  "emoncms_connected": 0,
  "packets_sent": 0,
  "packets_success": 0,
  "mqtt_connected": 1,
  "ohm_hour": "NotConnected",
  "free_heap": 20816,
  "comm_sent": 1077,
  "comm_success": 1075,
  "amp": 27500,
  "pilot": 32,
  "temp1": 247,
  "temp2": 0,
  "temp3": 230,
  "state": 3,
  "elapsed": 10790,
  "wattsec": 19800000,
  "watthour": 72970,
  "gfcicount": 0,
  "nogndcount": 0,
  "stuckcount": 0,
  "divertmode": 1
};

var autoService = 1;
var autoStart   = 0;
var serialDebug = 0;
var lcdType     = 0;
var commandEcho = 0;

var ffSupported = true;

let args = minimist(process.argv.slice(2), {
  alias: {
    h: "help",
    v: "version"
  },
  default: {
    help: false,
    version: false,
    port: 3000
  },
});

if(args.help) {
  console.log("OpenEVSE WiFi Simulator");
  return 0;
}

if(args.version) {
  console.log(config.version);
  return 0;
}

var port = args.port;

const app = express();
const expressWs = require("express-ws")(app);

function toHex(num, len)
{
  var str = num.toString(16);
  while(str.length < len) {
    str = "0" + str;
  }

  return str.toUpperCase();
}

function checksum(msg)
{
  var check = 0;
  for(var i = 0; i < msg.length; i++) {
    check ^= msg.charCodeAt(i);
  }

  var checkString = toHex(check, 2);

  return msg + "^" + checkString;
}

//
// Create HTTP server by ourselves.
//

// Setup the static content
app.use(express.static(path.join(__dirname, "../src/data"), { index: "home.htm" }));

// Setup the API endpoints
app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));

app.get("/config", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.json(config);
});
app.get("/status", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  status.srssi += 5 - (Math.floor(Math.random() * 11));
  res.json(status);
});
app.get("/update", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.send("<html><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware'> <input type='submit' value='Update'></form></html>");
});
app.post("/update", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});
app.get("/r", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");

  var dummyData = {
    "GT": "$OK 18 0 25 23 54 27^1B",
    "GE": "$OK 20 0229^2B",
    "GC": "$OK 10 80^29",
    "G3": "$OK 0^30",
    "GH": "$OK 0^30",
    "GO": "$OK 650 650^20",
    "GD": "$OK 0 0 0 0^20"
  };

  var rapi = req.query.rapi;

  status.comm_sent++;

  var regex = /\$([^\^]*)(\^..)?/;
  var match = rapi.match(regex);
  var request = match[1].split(" ");
  var cmd = request[0];
  var resp = { "cmd": rapi, "ret": "" };
  var success = false;

  switch (cmd) {
  case "GT": {
    var date = new Date();
    var time = [
      date.getFullYear() % 100,
      date.getMonth(),
      date.getDate(),
      date.getHours(),
      date.getMinutes(),
      date.getSeconds()
    ];
    resp.ret = checksum("$OK " + time.join(" "));
    success = true;
    break;
  }

  case "GE": {
    var flags = 0;
    flags |= (2 === config.service  ? 0x0001 : 0);
    flags |= (config.diodet         ? 0x0002 : 0);
    flags |= (config.ventt          ? 0x0004 : 0);
    flags |= (config.groundt        ? 0x0008 : 0);
    flags |= (config.relayt         ? 0x0010 : 0);
    flags |= (autoService           ? 0x0020 : 0);
    flags |= (autoStart             ? 0x0040 : 0);
    flags |= (serialDebug           ? 0x0080 : 0);
    flags |= (lcdType               ? 0x0100 : 0);
    flags |= (config.gfcit          ? 0x0200 : 0);
    flags |= (config.tempt          ? 0x0400 : 0);

    var flagsStr = toHex(flags, 4);

    resp.ret = checksum("$OK " + status.pilot.toString() + " " + flagsStr);
    success = true;
    break;
  }

  case "FF": {
    if(ffSupported && request.length >= 3)
    {
      switch(request[1])
      {
      case "D":
        config.diodet = parseInt(request[2]) ? 0 : 1;
        success = true;
        break;

      case "E":
        commandEcho = parseInt(request[2]);
        success = true;
        break;

      case "F":
        config.gfcit = parseInt(request[2]) ? 0 : 1;
        success = true;
        break;

      case "G":
        config.groundt = parseInt(request[2]) ? 0 : 1;
        success = true;
        break;

      case "R":
        config.relayt = parseInt(request[2]) ? 0 : 1;
        success = true;
        break;

      case "T":
        config.tempt = parseInt(request[2]) ? 0 : 1;
        success = true;
        break;

      case "V":
        config.ventt = parseInt(request[2]) ? 0 : 1;
        success = true;
        break;
      }

      if(success) {
        resp.ret = checksum("$OK");
      }
    }
  } break;

  case "SD": {
    if(!ffSupported && request.length >= 2) {
      config.diodet = parseInt(request[1]) ? 0 : 1;
      success = true;
      resp.ret = checksum("$OK");
    }
  } break;

  case "SE": {
    if(!ffSupported && request.length >= 2) {
      commandEcho = parseInt(request[1]);
      success = true;
      resp.ret = checksum("$OK");
    }
  } break;

  case "SF": {
    if(!ffSupported && request.length >= 2) {
      config.gfcit = parseInt(request[1]) ? 0 : 1;
      success = true;
      resp.ret = checksum("$OK");
    }
  } break;

  case "SG": {
    if(!ffSupported && request.length >= 2) {
      config.groundt = parseInt(request[1]) ? 0 : 1;
      success = true;
      resp.ret = checksum("$OK");
    }
  } break;

  case "SR": {
    if(!ffSupported && request.length >= 2) {
      config.relayt = parseInt(request[1]) ? 0 : 1;
      success = true;
      resp.ret = checksum("$OK");
    }
  } break;

  case "SV": {
    if(!ffSupported && request.length >= 2) {
      config.ventt = parseInt(request[1]) ? 0 : 1;
      success = true;
      resp.ret = checksum("$OK");
    }
  } break;

  default:
    if (rapi.hasOwnProperty(cmd)) {
      resp.ret = dummyData[cmd];
      success = true;
      break;
    }
  }

  if(success) {
    status.comm_success++;
  } else {
    resp.ret = "$NK";
  }
  res.json(resp);
});

app.post("/savenetwork", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/saveemoncms", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/savemqtt", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/saveadmin", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/saveohmkey", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/reset", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/restart", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.get("/scan", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  setTimeout(function () {
    res.json([{"rssi":-51,"ssid":"wibble_ext","bssid":"C4:04:15:5A:45:DE","channel":11,"secure":4,"hidden":false},{"rssi":-45,"ssid":"esplug_10560510","bssid":"1A:FE:34:A1:23:FE","channel":11,"secure":7,"hidden":false},{"rssi":-85,"ssid":"BTWifi-with-FON","bssid":"02:FE:F4:32:F1:08","channel":6,"secure":7,"hidden":false},{"rssi":-87,"ssid":"BTWifi-X","bssid":"22:FE:F4:32:F1:08","channel":6,"secure":7,"hidden":false},{"rssi":-75,"ssid":"wibble","bssid":"6C:B0:CE:20:7C:3A","channel":6,"secure":4,"hidden":false},{"rssi":-89,"ssid":"BTHub3-ZWCW","bssid":"00:FE:F4:32:F1:08","channel":6,"secure":8,"hidden":false}]);
  }, 5000);
});

app.post("/apoff", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.post("/divertmode", function (req, res) {
  res.header("Cache-Control", "no-cache, private, no-store, must-revalidate, max-stale=0, post-check=0, pre-check=0");
  res.status(500).send("Not implemented");
});

app.ws("/ws", function(ws, req) {
  ws.on("message", function(msg) {
    //ws.send(msg);
  });
});

app.listen(port, () => console.log("OpenEVSE WiFi Simulator listening on port " + port + "!"));
