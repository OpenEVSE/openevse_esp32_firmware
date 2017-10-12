/* global $, ko */
/* exported BaseViewModel */

function BaseViewModel(defaults, remoteUrl, mappings = {}) {
  "use strict";
  var self = this;
  self.remoteUrl = remoteUrl;

  // Observable properties
  ko.mapping.fromJS(defaults, mappings, self);
  self.fetching = ko.observable(false);
}

BaseViewModel.prototype.update = function (after = function () { }) {
  "use strict";
  var self = this;
  self.fetching(true);
  $.get(self.remoteUrl(), function (data) {
    ko.mapping.fromJS(data, self);
  }, "json").always(function () {
    self.fetching(false);
    after();
  });
};

