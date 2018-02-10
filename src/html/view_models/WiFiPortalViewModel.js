/* global $, ko, ConfigViewModel, StatusViewModel, WiFiScanViewModel, WiFiConfigViewModel */
/* exported WiFiPortalViewModel */

function WiFiPortalViewModel(baseHost, basePort)
{
  "use strict";
  var self = this;

  self.baseHost = ko.observable("" !== baseHost ? baseHost : "openevse.local");
  self.basePort = ko.observable(basePort);
  self.baseEndpoint = ko.pureComputed(function () {
    var endpoint = "//" + self.baseHost();
    if(80 !== self.basePort()) {
      endpoint += ":"+self.basePort();
    }
    return endpoint;
  });

  self.config = new ConfigViewModel(self.baseEndpoint);
  self.status = new StatusViewModel(self.baseEndpoint);
  self.scan = new WiFiScanViewModel(self.baseEndpoint);
  self.wifi = new WiFiConfigViewModel(self.baseEndpoint, self.config, self.status, self.scan);

  self.initialised = ko.observable(false);
  self.updating = ko.observable(false);

  var updateTimer = null;
  var updateTime = 5 * 1000;

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
}

