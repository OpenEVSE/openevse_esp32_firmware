/* global $, ko */
/* exported RapiViewModel */

function RapiViewModel(baseEndpoint) {
  "use strict";
  var self = this;

  self.baseEndpoint = baseEndpoint;

  self.rapiSend = ko.observable(false);
  self.cmd = ko.observable("");
  self.ret = ko.observable("");

  self.send = function() {
    self.rapiSend(true);
    $.get(self.baseEndpoint() + "/r?json=1&rapi="+encodeURI(self.cmd()), function (data) {
      self.ret(">"+data.ret);
      self.cmd(data.cmd);
    }, "json").always(function () {
      self.rapiSend(false);
    });
  };
}
