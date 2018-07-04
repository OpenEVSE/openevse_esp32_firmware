// OpenEVSE comms library
//
// Based (loosely) on https://github.com/tiramiseb/python-openevse/

/* global $ */
/* jshint node: true, bitwise: false*/

"use strict";

function OpenEVSEError(type, message = "") {
  this.type = type;
  this.message = message;
}

function OpenEVSERequest()
{
  var self = this;
  self._done = function() {};
  self._error = function() {};
  self._always = function() {};
  self.done = function(fn) {
    self._done = fn;
    return self;
  };
  self.error = function(fn) {
    self._error = fn;
    return self;
  };
  self.always = function(fn) {
    self._always = fn;
    return self;
  };
}

/* exported OpenEVSE */
function OpenEVSE(endpoint)
{
  var self = this;
  self._version = "0.1";
  self._endpoint = endpoint;
  self.states = {
    0: "unknown",
    1: "not connected",
    2: "connected",
    3: "charging",
    4: "vent required",
    5: "diode check failed",
    6: "gfci fault",
    7: "no ground",
    8: "stuck relay",
    9: "gfci self-test failure",
    10: "over temperature",
    254: "sleeping",
    255: "disabled"
  };
  self._lcd_colors = ["off", "red", "green", "yellow", "blue", "violet", "teal", "white"];
  self._status_functions = {"disable":"FD", "enable":"FE", "sleep":"FS"};
  self._lcd_types=["monochrome", "rgb"];
  self._service_levels=["A", "1", "2"];

  // Timeouts in seconds
  self.STANDARD_SERIAL_TIMEOUT = 0.5;
  self.RESET_SERIAL_TIMEOUT = 10;
  self.STATUS_SERIAL_TIMEOUT = 0;
  self.SYNC_SERIAL_TIMEOUT = 0.5;
  self.NEWLINE_MAX_AGE = 5;

  self.CORRECT_RESPONSE_PREFIXES = ("$OK", "$NK");

  self.regex = /\$([^\^]*)(\^..)?/;

  self._request = function(args, callback = function() {})
  {
    var command = "$" + (Array.isArray(args) ? args.join("+") : args);

    var request = new OpenEVSERequest();
    $.get(self._endpoint + "?json=1&rapi="+encodeURI(command), function (data) {
      var match = data.ret.match(self.regex);
      if(null !== match)
      {
        var response = match[1].split(" ");
        if("OK" === response[0]) {
          callback(response.slice(1));
          request._done(response.slice(1));
        } else {
          request._error(new OpenEVSEError("OperationFailed"));
        }
      } else {
        request._error(new OpenEVSEError("UnexpectedResponse"));
      }
    }, "json").always(function () {
      request._always();
    }).fail(function () {
      request._error(new OpenEVSEError("RequestFailed"));
    });

    return request;
  };

  /**
   * Get EVSE controller flags
   *
   * Specific values:
   * - service_level: 1 or 2
   * - lcd_type: 'monochrome' or 'rgb'
   *
   * True for enabled, False for disabled:
   * - auto_service_level
   * - diode_check
   * - gfi_self_test
   * - ground_check
   * - stuck_relay_check
   * - vent_required
   * - temp_check
   * - auto_start
   * - serial_debug
   */
  self._flags = function (callback)
  {
    var request = self._request("GE", function(data) {
      var flags = parseInt(data[1], 16);
      if(!isNaN(flags)) {
        var ret = {
          "service_level": (flags & 0x0001) + 1,
          "diode_check": 0 === (flags & 0x0002),
          "vent_required": 0 === (flags & 0x0004),
          "ground_check": 0 === (flags & 0x0008),
          "stuck_relay_check": 0 === (flags & 0x0010),
          "auto_service_level": 0 === (flags & 0x0020),
          "auto_start": 0 === (flags & 0x0040),
          "serial_debug": 0 !== (flags & 0x0080),
          "lcd_type": 0 !== (flags & 0x0100) ? "monochrome" : "rgb",
          "gfi_self_test": 0 === (flags & 0x0200),
          "temp_check": 0 === (flags & 0x0400)
        };

        callback(ret);
      } else {
        request._error(new OpenEVSEError("ParseError", "Failed to parse \""+data[0]+"\""));
      }
    });
    return request;
  };

  /*** Function operations ***/

  /**
   * Reset the OpenEVSE
   */
  self.reset = function ()
  {
    return self._request("FR");
  };

  /**
   * Set or get the RTC time
   *
   * Argument:
   *  - a Date object
   *
   * If the datetime object is not specified, get the current OpenEVSE clock
   *
   * Returns a datetime object
   */
  self.time = function(callback, date = false)
  {
    if(false !== date) {
      return self._request([
        "S1", date.getFullYear() - 2000,
        date.getMonth() + 1, date.getDate(),
        date.getHours(), date.getMinutes(),
        date.getSeconds()], function() {
        self.time(callback);
      });
    }

    var request = self._request("GT", function(data) {
      if(data.length >= 6) {
        var year = parseInt(data[0]);
        var month = parseInt(data[1]);
        var day = parseInt(data[2]);
        var hour = parseInt(data[3]);
        var minute = parseInt(data[4]);
        var second = parseInt(data[5]);

        if(!isNaN(year) && !isNaN(month) && !isNaN(day) && !isNaN(hour) && !isNaN(minute) && !isNaN(second)) {
          if (year==165 && month==165 && day==165 && hour==165 && minute==165 && second==85){
            var date = new Date(0);
            callback(date,false);//this pattern occurs when no RTC is connected to openevse
          }
          else{
            var date = new Date(2000+year, month - 1, day, hour, minute, second);
            callback(date,true);
          }
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse time \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Set or get the charge timer
   *
   * Argument:
   *  - start: The start time
   *  - end: The stop time
   *
   * If any of the values is false, get the timer
   */
  self.timer = function(callback, start = false, stop = false)
  {
    function addZero(val) {
      return (val < 10 ? "0" : "") + val;
    }

    if(false !== start && false !== stop) {
      var timeRegex = /([01]\d|2[0-3]):([0-5]\d)/;
      var startArray = start.match(timeRegex);
      var stopArray = stop.match(timeRegex);

      if(null !== startArray && null !== stopArray)
      {
        return self._request([
          "ST",
          parseInt(startArray[1]), parseInt(startArray[2]),
          parseInt(stopArray[1]), parseInt(stopArray[2])
        ], function() {
          self.timer(callback);
        });
      }

      return false;
    }

    var request = self._request("GD", function(data) {
      if(data.length >= 4) {
        var startMinute = parseInt(data[0]);
        var startSecond = parseInt(data[1]);
        var stopMinute = parseInt(data[2]);
        var stopSecond = parseInt(data[3]);

        if(!isNaN(startMinute) && !isNaN(startSecond) && !isNaN(stopMinute) && !isNaN(stopSecond)) {
          if(0 === startMinute && 0 === startSecond && 0 === stopMinute && 0 === stopSecond) {
            callback(false, "--:--", "--:--");
          } else {
            start = addZero(startMinute) + ":" + addZero(startSecond);
            stop = addZero(stopMinute) + ":" + addZero(stopSecond);

            callback(true, start, stop);
          }
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse time \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Cancel the timer
   */
  self.cancelTimer = function (callback) {
    return self._request([
      "ST", 0, 0, 0, 0], function() {
      callback();
    });
  };

  /**
   * Get or set the charge time limit, in minutes.
   *
   * This time is rounded to the nearest quarter hour.
   *
   * The maximum value is 3825 minutes.
   *
   * Returns the limit
   */
  self.time_limit = function(callback, limit = false) {
    if(false !== limit) {
      return self._request(["S3", Math.round(limit/15.0)],
      function() {
        self.time_limit(callback);
      });
    }

    var request = self._request("G3", function(data) {
      if(data.length >= 1) {
        var limit = parseInt(data[0]);

        if(!isNaN(limit)) {
          callback(limit * 15);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Get or set the charge limit, in kWh.
   *
   * 0 = no charge limit
   *
   * Returns the limit
   */
  self.charge_limit = function(callback, limit = false) {
    if(false !== limit) {
      return self._request(["SH", limit],
      function() {
        self.charge_limit(callback);
      });
    }

    var request = self._request("GH", function(data) {
      if(data.length >= 1) {
        var limit = parseInt(data[0]);

        if(!isNaN(limit)) {
          callback(limit);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Set or get the ammeter settings
   *
   * If either of the arguments is None, get the values instead of setting them.
   *
   * Returns scale factor and offset
   */
  self.ammeter_settings = function(callback, scaleFactor = false, offset = false) {
    if(false !== scaleFactor && false !== offset) {
      return self._request(["SA", scaleFactor, offset],
      function() {
        callback(scaleFactor, offset);
      });
    }

    var request = self._request("GA", function(data) {
      if(data.length >= 2) {
        var scaleFactor = parseInt(data[0]);
        var offset = parseInt(data[0]);

        if(!isNaN(scaleFactor) && !isNaN(offset)) {
          callback(scaleFactor, offset);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Set or get the current capacity
   *
   * If capacity is false, get the value
   *
   * Returns the capacity in amperes
   */
  self.current_capacity = function(callback, capacity = false) {
    if(false !== capacity) {
      return self._request(["SC", capacity],
      function() {
        self.current_capacity(callback);
      });
    }

    var request = self._request("GE", function(data) {
      if(data.length >= 1) {
        var capacity = parseInt(data[0]);

        if(!isNaN(capacity)) {
          callback(capacity);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Set or get the service level
   *
   * Allowed values:
   * - 0: Auto
   * - 1: Level 1, 120VAC 16A
   * - 2: Level 2, 208-240VAC 80A
   *
   * If the level is not specified, the current level is returned
   *
   * Returns the current service level: 0 for auto, 1 or 2
   */
  self.service_level = function(callback, level = false) {
    if(false !== level) {
      return self._request(["SL", self._service_levels[level]],
      function() {
        self.service_level(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.auto_service_level ? 0 : flags.service_level, flags.service_level);
    });
    return request;
  };

  /**
   * Get the current capacity range, in amperes
   * (it depends on the service level)
   * Returns the current capacity:
   *     (min_capacity, max_capacity)
   */
  self.current_capacity_range = function(callback) {
    var request = self._request("GC", function(data) {
      if(data.length >= 2) {
        var minCapacity = parseInt(data[0]);
        var maxCapacity = parseInt(data[1]);
        if(!isNaN(minCapacity) && !isNaN(maxCapacity)) {
          callback(minCapacity, maxCapacity);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   * Change the EVSE status.
   *
   * If an action is not specified, the status is requested
   *
   * Allowed actions:
   *   * enable
   *   * disable
   *   * sleep
   *
   * Default: no action, request the status
   *
   * Returns the status of the EVSE as a string
   *
   */
  self.status = function(callback, action = false) {
    if(false !== action) {
      var cmd = self._status_functions[action]
      return self._request([cmd],
      function() {
        self.status(callback);
      });
    }

    var request = self._request("GS", function(data) {
      if(data.length >= 1) {
        var state = parseInt(data[0]);
        if(!isNaN(state)) {
          callback(state);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });

    return request;
  };

  /**
   * if enabled == True, enable the diode check
   * if enabled == False, disable the diode check
   * if enabled is not specified, request the diode check status
   *
   * Returns the diode check status
   */
  self.diode_check = function(callback, enabled = null) {
    if(null !== enabled) {
      return self._request(["FF", "D", enabled ? "1" : "0"],
      // OLD API < 4.0.1
      // return self._request(["SD", enabled ? "1" : "0"],
      function() {
        self.diode_check(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.diode_check);
    });
    return request;
  };

  /**
   * if enabled == True, enable the GFI self test
   * if enabled == False, disable the GFI self test
   * if enabled is not specified, request the GFI self test status
   *
   * Returns the GFI self test status
   */
  self.gfi_self_test = function(callback, enabled = null) {
    if(null !== enabled) {
      return self._request(["FF F", enabled ? "1" : "0"],
      // OLD API < 4.0.1
      // return self._request(["SF", enabled ? "1" : "0"],
      function() {
        self.gfi_self_test(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.gfi_self_test);
    });
    return request;
  };

  /**
   * if enabled == True, enable the ground check
   * if enabled == False, disable the ground check
   * if enabled is not specified, request the ground check status
   *
   * Returns the ground check status
   */
  self.ground_check = function(callback, enabled = null) {
    if(null !== enabled) {
      return self._request(["FF G", enabled ? "1" : "0"],
      // OLD API < 4.0.1
      // return self._request(["SG", enabled ? "1" : "0"],
      function() {
        self.ground_check(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.ground_check);
    });
    return request;
  };

  /**
   * if enabled == True, enable the stuck relay check
   * if enabled == False, disable the stuck relay check
   * if enabled is not specified, request the stuck relay check status
   *
   * Returns the stuck relay check status
   */
  self.stuck_relay_check = function(callback, enabled = null) {
    if(null !== enabled) {
      return self._request(["FF R", enabled ? "1" : "0"],
      // OLD API < 4.0.1
      // return self._request(["SR", enabled ? "1" : "0"],
      function() {
        self.stuck_relay_check(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.stuck_relay_check);
    });
    return request;
  };

  /**
   * if enabled == True, enable "ventilation required check"
   * if enabled == False, disable "ventilation required check"
   * if enabled is not specified, request the "ventilation required check" status
   *
   * Returns the "ventilation required" status
   */
  self.vent_required = function(callback, enabled = null) {
    if(null !== enabled) {
      return self._request(["FF V", enabled ? "1" : "0"],
      // OLD API < 4.0.1
      // return self._request(["SV", enabled ? "1" : "0"],
      function() {
        self.vent_required(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.vent_required);
    });
    return request;
  };

  /**
   * if enabled == True, enable "temperature monitoring"
   * if enabled == False, disable "temperature monitoring"
   * if enabled is not specified, request the "temperature monitoring" status
   *
   * Returns the "temperature monitoring" status
   */

  self.temp_check = function(callback, enabled = null) {
    if(null !== enabled) {
      return self._request(["FF T", enabled ? "1" : "0"],
      function() {
        self.temp_check(callback);
      });
    }

    var request = self._flags(function(flags) {
      callback(flags.temp_check);
    });
    return request;
  };


  // OLD API < 4.0.1
  // self.temp_check = function(callback, enabled = null) {
  //   if(null !== enabled)
  //   {
  //     if(enabled)
  //     {
  //       return self._request("GO", function(data) {
  //         self._request(["SO", data[0], data[1]],
  //           function() {
  //             self.temp_check(callback);
  //           });
  //       });
  //     }
  // **NOTE: SO has been removed totally in RAPI 4.0.0**
  //     return self._request(["SO", "0", "0"],
  //       function() {
  //         self.temp_check(callback);
  //       });
  //   }
  //   var request = self._flags(function(flags) {
  //     callback(flags.temp_check);
  //   });
  //   return request;
  // };



  /**
   *
   */
  self.over_temperature_thresholds = function(callback, ambientthresh = false, irthresh = false) {
    if(false !== ambientthresh && false !== irthresh) {
      return self._request(["SO", ambientthresh, irthresh],
      function() {
        self.over_temperature_thresholds(callback);
      });
    }

    var request = self._request("GO", function(data) {
      if(data.length >= 2) {
        var ambientthresh = parseInt(data[0]);
        var irthresh = parseInt(data[0]);

        if(!isNaN(ambientthresh) && !isNaN(irthresh)) {
          callback(ambientthresh, irthresh);
        } else {
          request._error(new OpenEVSEError("ParseError", "Could not parse \""+data.join(" ")+"\" arguments"));
        }
      } else {
        request._error(new OpenEVSEError("ParseError", "Only received "+data.length+" arguments"));
      }
    });
    return request;
  };

  /**
   *
   */
  self.press_button = function(callback) {
    var request = self._request("F1", function() {
      callback();
    });
    return request;
  };

  self.setEndpoint = function (endpoint) {
    self._endpoint = endpoint;
  };
}
