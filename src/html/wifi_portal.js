/* global $, ko, WiFiPortalViewModel */

(function() {
  "use strict";

// Configure the endpoint to use, for dev you can change to point at a remote ESP
// and run the HTML/JS from file, no need to upload to the ESP to test

var baseHost = window.location.hostname;
// var baseHost = "openevse.local";
//var baseHost = "192.168.4.1";
//var baseHost = "172.16.0.57";

var basePort = window.location.port;

$(function () {
  // Activates knockout.js
  var openevse = new WiFiPortalViewModel(baseHost, basePort);
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
