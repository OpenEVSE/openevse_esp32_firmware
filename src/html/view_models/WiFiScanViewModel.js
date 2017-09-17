/* global $, ko */
/* exported WiFiScanViewModel */

function WiFiScanResultViewModel(data)
{
  "use strict";
  var self = this;
  ko.mapping.fromJS(data, {}, self);
}

function WiFiScanViewModel(baseEndpoint)
{
  "use strict";
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
      if(data.length > 0) {
        ko.mapping.fromJS(data, self.results);
        self.results.sort(function (left, right) {
          if(left.ssid() === right.ssid()) {
            return left.rssi() < right.rssi() ? 1 : -1;
          }
          return left.ssid() < right.ssid() ? -1 : 1;
        });
      }
    }, "json").always(function () {
      self.fetching(false);
      after();
    });
  };
}
