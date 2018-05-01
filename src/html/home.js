/* global $, ko, OpenEvseWiFiViewModel */

(function() {
  "use strict";

// Configure the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
//var baseHost = "openevse.local";
//var baseHost = "192.168.4.1";
//var baseHost = "172.16.0.70";

var basePort = window.location.port;
var baseProtocol = window.location.protocol;

// DEBUG
// console.log(baseHost);
// console.log(basePort);
// console.log(baseProtocol);

$(function () {
  // Activates knockout.js
  var openevse = new OpenEvseWiFiViewModel(baseHost, basePort, baseProtocol);
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

// Based on https://github.com/emoncms/emoncms/blob/master/Lib/tablejs/custom-table-fields.js#L176-L203
/* exported formatUpdate */
function formatUpdate(secs) {
  "use strict";

  if(false === secs) {
    return "N/A";
  }

  var mins = secs/60;
  var hour = secs/3600;
  var day = hour/24;

  var updated = secs.toFixed(0) + "s";
  if (secs.toFixed(0) === 0) {
    updated = "now";
  } else if (day>7) {
    updated = "inactive";
  } else if (day>2) {
    updated = day.toFixed(1)+" days";
  } else if (hour>2) {
    updated = hour.toFixed(0)+" hrs";
  } else if (secs>180) {
    updated = mins.toFixed(0)+" mins";
  }

  return updated;
}

// Based on https://github.com/emoncms/emoncms/blob/master/Lib/tablejs/custom-table-fields.js#L176-L203
/* exported updateClass */
function updateClass(secs) {
  "use strict";

  if(false === secs) {
    return "";
  }

  secs = Math.abs(secs);
  var updateClass = "updateBad";
  if (secs<25) {
    updateClass = "updateGood";
  } else if (secs<60) {
    updateClass = "updateSlow";
  } else if (secs<(3600*2)) {
    updateClass = "updateSlower";
  }

  return updateClass;
}
