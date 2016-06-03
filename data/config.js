var statusupdate = false;
var selected_network_ssid = "";
var lastmode = "";
var ipaddress = "";

var r1 = new XMLHttpRequest(); 
r1.open("GET", "status", false);
r1.onreadystatechange = function () {
  if (r1.readyState != 4 || r1.status != 200) return;
  var status = JSON.parse(r1.responseText);
  
  document.getElementById("passkey").value = status.pass;
  document.getElementById("apikey").value = status.apikey;
  document.getElementById("node").value = status.node;
  document.getElementById("ohmkey").value = status.ohmkey;
  document.getElementById("ohmhour").innerHTML = status.ohmhour;

  if (status.mode=="AP") {
      document.getElementById("mode").innerHTML = "Access Point (AP)";
      document.getElementById("client-view").style.display = 'none';
      document.getElementById("ap-view").style.display = '';
      
      var out = "";
      for (var z in status.networks) {
          out += "<tr><td><input class='networkcheckbox' name='"+status.networks[z]+"' type='checkbox'></td><td>"+status.networks[z]+"</td></tr>";
      }
      document.getElementById("networks").innerHTML = out;
  } else {
      document.getElementById("mode").innerHTML = "Client (STA)";
      document.getElementById("sta-ssid").innerHTML = status.ssid;
      document.getElementById("sta-ip").innerHTML = "<a href='http://"+status.ipaddress+"'>"+status.ipaddress+"</a>";
      document.getElementById("comm-psent").innerHTML = status.comm_sent;
      document.getElementById("comm-psuccess").innerHTML = status.comm_success;
      document.getElementById("sta-psent").innerHTML = status.packets_sent;
      document.getElementById("sta-psuccess").innerHTML = status.packets_success;
      document.getElementById("ap-view").style.display = 'none';
      document.getElementById("client-view").style.display = '';
      ipaddress = status.ipaddress;
  }
};
r1.send();


update();
setInterval(update,10000);


// -----------------------------------------------------------------------
// Periodic 10s update of last data values
// -----------------------------------------------------------------------
function update() {

    var r = new XMLHttpRequest(); 
    r.open("GET", "lastvalues", true);
    r.onreadystatechange = function () {
	    if (r.readyState != 4 || r.status != 200) return;
	    var str = r.responseText;
	    var namevaluepairs = str.split(",");
	    var out = "";
	    for (var z in namevaluepairs) {
	        var namevalue = namevaluepairs[z].split(":");
	        var units = "";
                if (namevalue[0].indexOf("Pilot")==0) units = "A";
	        if (namevalue[0].indexOf("CT")==0) units = "w";
	        if (namevalue[0].indexOf("T")==0) units = "&deg;C";
	        out += "<tr><td>"+namevalue[0]+"</td><td>"+namevalue[1]+units+"</td></tr>";
	    }
	    document.getElementById("datavalues").innerHTML = out;
    };
    r.send();
	
	var r1 = new XMLHttpRequest(); 
    r1.open("GET", "status", false);
    r1.onreadystatechange = function () {
    if (r1.readyState != 4 || r1.status != 200) return;
      var status = JSON.parse(r1.responseText);
	  document.getElementById("comm-psent").innerHTML = status.comm_sent;
      document.getElementById("comm-psuccess").innerHTML = status.comm_success;
      document.getElementById("sta-psent").innerHTML = status.packets_sent;
      document.getElementById("sta-psuccess").innerHTML = status.packets_success;
	  document.getElementById("ohmhour").innerHTML = status.ohmhour;
    };
    r1.send();
	
    var r2 = new XMLHttpRequest(); 
    r2.open("GET", "config", true);
    r2.timeout = 2000;
    r2.onreadystatechange = function () {
            if (r2.readyState != 4 || r2.status != 200) return;
              var config = JSON.parse(r2.responseText);
              document.getElementById("firmware").innerHTML = config.firmware;
              document.getElementById("protocol").innerHTML = config.protocol;
              document.getElementById("diodet").innerHTML = config.diodet;
              document.getElementById("gfcit").innerHTML = config.gfcit;
              document.getElementById("groundt").innerHTML = config.groundt;
              document.getElementById("relayt").innerHTML = config.relayt;
              document.getElementById("ventt").innerHTML = config.ventt;
              document.getElementById("tempt").innerHTML = config.tempt;
      
  };
  r2.send();
}

  



// -----------------------------------------------------------------------
// Event: Connect
// -----------------------------------------------------------------------
document.getElementById("connect").addEventListener("click", function(e) {
    var passkey = document.getElementById("passkey").value;
    if (selected_network_ssid=="") {
        alert("Please select network");
    } else {
        document.getElementById("ap-view").style.display = 'none';
        document.getElementById("wait-view").style.display = '';
        
        var r = new XMLHttpRequest(); 
        r.open("POST", "savenetwork", false);
        r.setRequestHeader("Content-type","application/x-www-form-urlencoded");
        r.onreadystatechange = function () {
	        if (r.readyState != 4 || r.status != 200) return;
	        var str = r.responseText;
	        console.log(str);
	        
	        statusupdate = setInterval(updateStatus,5000);
        };
        r.send("ssid="+selected_network_ssid+"&pass="+passkey);
    }
});

// -----------------------------------------------------------------------
// Event: Apikey save
// -----------------------------------------------------------------------
document.getElementById("save-apikey").addEventListener("click", function(e) {
    var apikey = document.getElementById("apikey").value;
    var node = document.getElementById("node").value;
    if (apikey=="") alert("Please enter apikey");
    
    var r = new XMLHttpRequest(); 
    r.open("POST", "saveapikey", true);
    r.setRequestHeader("Content-type","application/x-www-form-urlencoded");
    r.onreadystatechange = function () {};
    r.send("&apikey="+apikey+"&node="+node);

});

// -----------------------------------------------------------------------
// Event: Save Ohm Connect Key
// -----------------------------------------------------------------------
document.getElementById("save-ohmkey").addEventListener("click", function(e) {
    var ohmkey = document.getElementById("ohmkey").value;
    var r = new XMLHttpRequest(); 
    r.open("POST", "saveohmkey", true);
    r.setRequestHeader("Content-type","application/x-www-form-urlencoded");
    r.onreadystatechange = function () {};
    r.send("&ohm="+ohmkey);

});

// -----------------------------------------------------------------------
// Event: Turn off Access Point
// -----------------------------------------------------------------------
document.getElementById("apoff").addEventListener("click", function(e) {    
    var r = new XMLHttpRequest(); 
    r.open("POST", "apoff", true);
    r.onreadystatechange = function () {
        if (r.readyState != 4 || r.status != 200) return;
        var str = r.responseText;
        console.log(str);
        document.getElementById("apoff").style.display = 'none';
        if (ipaddress!="") window.location = ipaddress;
        
	  };
    r.send();
});
// -----------------------------------------------------------------------
// Event: Reset config and reboot
// -----------------------------------------------------------------------
document.getElementById("reset").addEventListener("click", function(e) {    
    var r = new XMLHttpRequest(); 
    r.open("POST", "reset", true);
    r.onreadystatechange = function () {
        if (r.readyState != 4 || r.status != 200) return;
        var str = r.responseText;
        console.log(str);
	  };
    r.send();
});

// -----------------------------------------------------------------------
// UI: Network select
// -----------------------------------------------------------------------
var networkcheckboxes = document.getElementsByClassName("networkcheckbox");

var networkSelect = function() {
    selected_network_ssid = this.getAttribute("name");
    
    for (var i = 0; i < networkcheckboxes.length; i++) {
        if (networkcheckboxes[i].getAttribute("name")!=selected_network_ssid)
            networkcheckboxes[i].checked = 0;
    }
};

for (var i = 0; i < networkcheckboxes.length; i++) {
    networkcheckboxes[i].addEventListener('click', networkSelect, false);
}

// lastupdated

function list_format_updated(time){
  time = time * 1000;
  var servertime = (new Date()).getTime() - table.timeServerLocalOffset;
  var update = (new Date(time)).getTime();

  var secs = (servertime-update)/1000;
  var mins = secs/60;
  var hour = secs/3600;
  var day = hour/24;

  var updated = secs.toFixed(0) + "s";
  if ((update == 0) || (!$.isNumeric(secs))) updated = "n/a";
  else if (secs< 0) updated = secs.toFixed(0) + "s"; // update time ahead of server date is signal of slow network
  else if (secs.toFixed(0) == 0) updated = "now";
  else if (day>7) updated = "inactive";
  else if (day>2) updated = day.toFixed(1)+" days";
  else if (hour>2) updated = hour.toFixed(0)+" hrs";
  else if (secs>180) updated = mins.toFixed(0)+" mins";

  secs = Math.abs(secs);
  var color = "rgb(255,0,0)";
  if (secs<25) color = "rgb(50,200,50)"
  else if (secs<60) color = "rgb(240,180,20)"; 
  else if (secs<(3600*2)) color = "rgb(255,125,20)"

  return "<span style='color:"+color+";'>"+updated+"</span>";
}


