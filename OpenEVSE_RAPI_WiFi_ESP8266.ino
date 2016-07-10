/*
 * Copyright (c) 2015 Chris Howell
 *
 * This file is part of Open EVSE.
 * Open EVSE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * Open EVSE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with Open EVSE; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
 
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> 
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include "FS.h"
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>

ADC_MODE(ADC_VCC);

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
int commDelay = 60;

const char* fwversion = "D1.0.3";

//Default SSID and PASSWORD for AP Access Point Mode
const char* ssid = "OpenEVSE";
const char* password = "openevse";
const char* www_username = "admin";
const char* www_password = "openevse";
String st;

//EEPROM Strings
String esid = "";
String epass = "";  
String apikey = "";
String node = "";

int espvcc = 0;
int espflash = 0;
int espfree = 0;

String connected_network = "";
String last_datastr = "";
String status_string = "";
String ipaddress = "";

//SERVER strings and interfers for OpenEVSE Energy Monotoring
const char* host = "data.openevse.com"; //Default to use the OpenEVSE EmonCMS Server
//const char* host = "www.emoncms.org"; //Optional to use Open Energy EmonCMS servers
//const char* host = "192.168.1.123";  //Optional to use your own EmonCMS server
const int httpsPort = 443;
const char* e_url = "/emoncms/input/post.json?node=";


int amp = 0; //OpenEVSE Current Sensor
int volt = 0; //Not currently in used
int temp1 = 0; //Sensor DS3232 Ambient
int temp2 = 0; //Sensor MCP9808 Ambient
int temp3 = 0; //Sensor TMP007 Infared
int pilot = 0; //OpenEVSE Pilot Setting
long state = 0; //OpenEVSE State
String estate = "Unknown"; // Common name for State

//Server strings for Ohm Connect 
const char* ohm_host = "login.ohmconnect.com";
const char* ohm_url = "/verify-ohm-hour/";
String ohm = "";
const int ohm_httpsPort = 443;
const char* ohm_fingerprint = "6B 39 04 A4 BB E0 87 B2 EB B6 FE 77 CD D5 F6 A7 22 4B 3B ED";
String ohm_hour = "NotConnected";
int evse_sleep = 0;

//Defaults OpenEVSE Settings
byte rgb_lcd = 1;
byte serial_dbg = 0;
byte auto_service = 1;
int service = 1;
int current_l1 = 0;
int current_l2 = 0;
String current_l1min = "-";
String current_l2min = "-";
String current_l1max = "-";
String current_l2max = "-";
String current_scale = "-";
String current_offset = "-";

//Default OpenEVSE Safety Configuration
byte diode_ck = 1;
byte gfci_test = 1;
byte ground_ck = 1;
byte stuck_relay = 1;
byte vent_ck = 1;
byte temp_ck = 1;
byte auto_start = 1;

String firmware = "-";
String protocol = "-";

//Default OpenEVSE Fault Counters
String gfci_count = "-";
String nognd_count = "-";
String stuck_count = "-";

//OpenEVSE Session options
String kwh_limit = "0";
String time_limit = "0";

//OpenEVSE Usage Statistics
String wattsec = "0";
String watthour_total = "0";

// Wifi mode
// 0 - STA (Client)
// 1 - AP with STA retry
// 2 - AP only
// 3 - AP + STA
int wifi_mode = 0; 
int buttonState = 0;
int clientTimeout = 0;
int i = 0;
unsigned long Timer;
unsigned long Timer2;
unsigned long packets_sent = 0;
unsigned long packets_success = 0;
unsigned long comm_sent = 0;
unsigned long comm_success = 0;

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}
// -------------------------------------------------------------------
// Start Access Point, starts on 192.168.4.1
// Access point is used for wifi network selection
// -------------------------------------------------------------------
void startAP() {
  //Serial.print("Starting Access Point");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  //Serial.print("Scan: ");
  int n = WiFi.scanNetworks();
  //Serial.print(n);
  //Serial.println(" networks found");
  st = "";
  for (int i = 0; i < n; ++i){
    st += "\""+WiFi.SSID(i)+"\"";
    if (i<n-1) st += ",";
  }
  delay(100);
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  char tmpStr[40];
  Serial.println("$FP 0 0 SSID...OpenEVSE.");
  delay(100);
  Serial.println("$FP 0 1 PASS...openevse.");
  delay(5000);
  Serial.println("$FP 0 0 IP_Address......");
  delay(100);
  sprintf(tmpStr,"%d.%d.%d.%d",myIP[0],myIP[1],myIP[2],myIP[3]);
  //Serial.print("Access Point IP Address: ");
  Serial.print("$FP 0 1 ");
  Serial.println(tmpStr);
  ipaddress = tmpStr;
}

// -------------------------------------------------------------------
// Start Client, attempt to connect to Wifi network
// -------------------------------------------------------------------
void startClient() {
  //Serial.print("Connecting as Wifi Client to ");
  //Serial.print(esid.c_str());
  //Serial.print(" epass:");
  //Serial.println(epass.c_str());
  WiFi.begin(esid.c_str(), epass.c_str());
  
  delay(50);
  
  int t = 0;
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED){
    
    delay(500);
    t++;
    if (t >= 20){
      //Serial.println(" ");
      //Serial.println("Trying Again...");
      delay(2000);
      WiFi.disconnect();
      WiFi.begin(esid.c_str(), epass.c_str());
      t = 0;
      attempt++;
      if (attempt >= 5){
        startAP();
        // AP mode with SSID in EEPROM, connection will retry in 5 minutes
        wifi_mode = 1;
        break;
      }
    }
  }
  
  if (wifi_mode == 0 || wifi_mode == 3){
    IPAddress myAddress = WiFi.localIP();
    char tmpStr[40];
    sprintf(tmpStr,"%d.%d.%d.%d",myAddress[0],myAddress[1],myAddress[2],myAddress[3]);
    //Serial.print("Connected, IP Address: ");
    Serial.println("$FP 0 0 Client-IP.......");
    delay(100);
    Serial.print("$FP 0 1 ");
    Serial.println(tmpStr);
    
    // Copy the connected network and ipaddress to global strings for use in status request
    connected_network = esid;
    ipaddress = tmpStr;
  }
}

void ResetEEPROM(){
  //Serial.println("Erasing EEPROM");
  for (int i = 0; i < 512; ++i) { 
    EEPROM.write(i, 0);
    //Serial.print("#"); 
  }
  EEPROM.commit();   
}

// -------------------------------------------------------------------
// Load SPIFFS Home page
// url: /
// -------------------------------------------------------------------
void handleHome() {
  String s;
  File f = SPIFFS.open("/home.html", "r");
  if (f) {
    String s = f.readString();
    server.send(200, "text/html", s);
    f.close();
  }
}

// -------------------------------------------------------------------
// Handle turning Access point off
// url: /apoff
// -------------------------------------------------------------------
void handleAPOff() {
  server.send(200, "text/html", "Turning Access Point Off");
  Serial.println("Turning Access Point Off");
  delay(2000);
  WiFi.mode(WIFI_STA); 
}

// -------------------------------------------------------------------
// Save selected network to EEPROM and attempt connection
// url: /savenetwork
// -------------------------------------------------------------------
void handleSaveNetwork() {
  String s;
  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");      
  esid = qsid;
  epass = qpass;
  
  qpass.replace("%21", "!");
//  qpass.replace("%22", '"');
  qpass.replace("%23", "#");
  qpass.replace("%24", "$");
  qpass.replace("%25", "%");
  qpass.replace("%26", "&");
  qpass.replace("%27", "'");
  qpass.replace("%28", "(");
  qpass.replace("%29", ")");
  qpass.replace("%2A", "*");
  qpass.replace("%2B", "+");
  qpass.replace("%2C", ",");
  qpass.replace("%2D", "-");
  qpass.replace("%2E", ".");
  qpass.replace("%2F", "/");
  qpass.replace("%3A", ":");
  qpass.replace("%3B", ";");
  qpass.replace("%3C", "<");
  qpass.replace("%3D", "=");
  qpass.replace("%3E", ">");
  qpass.replace("%3F", "?");
  qpass.replace("%40", "@");
  qpass.replace("%5B", "[");
  qpass.replace("%5C", "'\'");
  qpass.replace("%5D", "]");
  qpass.replace("%5E", "^");
  qpass.replace("%5F", "_");
  qpass.replace("%60", "`");
  qpass.replace("%7B", "{");
  qpass.replace("%7C", "|");
  qpass.replace("%7D", "}");
  qpass.replace("%7E", "~");
  qpass.replace('+', ' ');

  qsid.replace("%21", "!");
//  qsid.replace("%22", '"');
  qsid.replace("%23", "#");
  qsid.replace("%24", "$");
  qsid.replace("%25", "%");
  qsid.replace("%26", "&");
  qsid.replace("%27", "'");
  qsid.replace("%28", "(");
  qsid.replace("%29", ")");
  qsid.replace("%2A", "*");
  qsid.replace("%2B", "+");
  qsid.replace("%2C", ",");
  qsid.replace("%2D", "-");
  qsid.replace("%2E", ".");
  qsid.replace("%2F", "/");
  qsid.replace("%3A", ":");
  qsid.replace("%3B", ";");
  qsid.replace("%3C", "<");
  qsid.replace("%3D", "=");
  qsid.replace("%3E", ">");
  qsid.replace("%3F", "?");
  qsid.replace("%40", "@");
  qsid.replace("%5B", "[");
  qsid.replace("%5C", "'\'");
  qsid.replace("%5D", "]");
  qsid.replace("%5E", "^");
  qsid.replace("%5F", "_");
  qsid.replace("%60", "`");
  qsid.replace("%7B", "{");
  qsid.replace("%7C", "|");
  qsid.replace("%7D", "}");
  qsid.replace("%7E", "~");
  qsid.replace('+', ' ');
  
  if (qsid != 0){
    for (int i = 0; i < 32; i++){
      if (i<qsid.length()) {
        EEPROM.write(i+0, qsid[i]);
      } else {
        EEPROM.write(i+0, 0);
      }
    }
    
    for (int i = 0; i < 32; i++){
      if (i<qpass.length()) {
        EEPROM.write(i+32, qpass[i]);
      } else {
        EEPROM.write(i+32, 0);
      }
    }
    
    
    EEPROM.commit();
    server.send(200, "text/html", "Saved");
    delay(2000);
    
    // Startup in STA + AP mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password);
    wifi_mode = 3;
    startClient();
  }
}

// -------------------------------------------------------------------
// Save apikey
// url: /saveapikey
// -------------------------------------------------------------------
void handleSaveApikey() {
  apikey = server.arg("apikey");
  node = server.arg("node");
  if (apikey!=0) {
    EEPROM.write(129, node[i]);
    for (int i = 0; i < 32; i++){
      if (i<apikey.length()) {
        EEPROM.write(i+96, apikey[i]);
      } else {
        EEPROM.write(i+96, 0);
        EEPROM.write(129, 0);
      }      
    }
    EEPROM.commit();
    server.send(200, "text/html", "Saved");
  }
}
void handleSaveOhmkey() {
  ohm = server.arg("ohm");
  if (ohm!=0) {
    for (int i = 0; i < 8; i++){
      if (i<ohm.length()) {
        EEPROM.write(i+130, ohm[i]);
      } else {
        EEPROM.write(i+130, 0);
      }
    }
   EEPROM.commit();
   server.send(200, "text/html", "Saved");
}
}

// -------------------------------------------------------------------
// Wifi scan /scan not currently used
// url: /scan
// -------------------------------------------------------------------
void handleScan() {
  //Serial.println("WIFI Scan");
  int n = WiFi.scanNetworks();
  //Serial.print(n);
  //Serial.println(" networks found");
  st = "";
  for (int i = 0; i < n; ++i){
    st += "\""+WiFi.SSID(i)+"\"";
    if (i<n-1) st += ",";
  }
  server.send(200, "text/plain","["+st+"]");
}


// -------------------------------------------------------------------
// url: /status
// returns wifi status
// -------------------------------------------------------------------
void handleStatus() {

  String s = "{";
  if (wifi_mode==0) {
    s += "\"mode\":\"STA\",";
  } else if (wifi_mode==1 || wifi_mode==2) {
    s += "\"mode\":\"AP\",";
  } else if (wifi_mode==3) {
    s += "\"mode\":\"STA+AP\",";
  }
  s += "\"networks\":["+st+"],";
  s += "\"ssid\":\""+esid+"\",";
  s += "\"pass\":\""+epass+"\",";
  s += "\"apikey\":\""+apikey+"\",";
  s += "\"node\":\""+node+"\",";
  s += "\"ohmkey\":\""+ohm+"\",";
  s += "\"espflash\":\""+String(espflash)+"\",";
  s += "\"ipaddress\":\""+ipaddress+"\",";
  s += "\"version\":\""+String(fwversion)+"\"";
  s += "}";
  server.send(200, "text/html", s);
}

void handleConfig() {

  String s = "{";
  s += "\"firmware\":\""+firmware+"\",";
  s += "\"protocol\":\""+protocol+"\",";
  s += "\"diodet\":\""+String(diode_ck)+"\",";
  s += "\"gfcit\":\""+String(gfci_test)+"\",";
  s += "\"groundt\":\""+String(ground_ck)+"\",";
  s += "\"relayt\":\""+String(stuck_relay)+"\",";
  s += "\"ventt\":\""+String(vent_ck)+"\",";
  s += "\"tempt\":\""+String(temp_ck)+"\",";
  s += "\"service\":\""+String(service)+"\",";
  s += "\"l1min\":\""+current_l1min+"\",";
  s += "\"l1max\":\""+current_l1max+"\",";
  s += "\"l2min\":\""+current_l2min+"\",";
  s += "\"l2max\":\""+current_l2max+"\",";
  s += "\"scale\":\""+current_scale+"\",";
  s += "\"offset\":\""+current_offset+"\",";
  s += "\"gfcicount\":\""+gfci_count+"\",";
  s += "\"nogndcount\":\""+nognd_count+"\",";
  s += "\"stuckcount\":\""+stuck_count+"\",";
  s += "\"kwhlimit\":\""+kwh_limit+"\",";
  s += "\"timelimit\":\""+time_limit+"\"";
  s += "}";
  s.replace(" ", "");
  server.send(200, "text/html", s);
 }
  
 void handleUpdate() {
    
  String s = "{";
  s += "\"ohmhour\":\""+ohm_hour+"\",";
  s += "\"espvcc\":\""+String(espvcc)+"\",";
  s += "\"espfree\":\""+String(espfree)+"\",";
  s += "\"comm_sent\":\""+String(comm_sent)+"\",";
  s += "\"comm_success\":\""+String(comm_success)+"\",";
  s += "\"packets_sent\":\""+String(packets_sent)+"\",";
  s += "\"packets_success\":\""+String(packets_success)+"\",";
  s += "\"amp\":\""+String(amp)+"\",";
  s += "\"pilot\":\""+String(pilot)+"\",";
  s += "\"temp1\":\""+String(temp1)+"\",";
  s += "\"temp2\":\""+String(temp2)+"\",";
  s += "\"temp3\":\""+String(temp3)+"\",";
  s += "\"estate\":\""+String(estate)+"\",";
  s += "\"wattsec\":\""+wattsec+"\",";
  s += "\"watthour\":\""+watthour_total+"\"";
  s += "}";
  s.replace(" ", "");
 server.send(200, "text/html", s); 
}

// -------------------------------------------------------------------
// Reset config and reboot
// url: /reset
// -------------------------------------------------------------------
void handleRst() {
  ResetEEPROM();
  EEPROM.commit();
  server.send(200, "text/html", "Reset");
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
}

void handleRapi() {
  String s;
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Send RAPI Command<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
        s += "<p>";
        s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
        s += "</html>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRapiR() {
  String s;
  String rapiString;
  String rapi = server.arg("rapi");
  rapi.replace("%24", "$");
  rapi.replace("+", " "); 
  Serial.flush();
  Serial.println(rapi);
  delay(commDelay);
       while(Serial.available()) {
         rapiString = Serial.readStringUntil('\r');
       }    
   s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
   s += "<p>";
   s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
   s += rapi;
   s += "<p>>";
   s += rapiString;
   s += "<p></html>\r\n\r\n";
   server.send(200, "text/html", s);
}

void handleDateTime(){
  String s;
// variables for command responses
  String sFirst = "0";
  String sSecond = "0";
  String sThird = "0";
  String sFourth = "0";
  String sFifth = "0";
  String sSixth = "0";
  s = "<HTML>";
  s +="<h2>Date and Time</h2>";  
 //get date and time
  Serial.flush();
  Serial.println("$GT^37");
  delay(commDelay);
  int month = 0;
  int day = 0;
  int year = 0;
  int hour = 0;
  int minutes = 0;
  int index;
  while(Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      int first_blank_index = rapiString.indexOf(' ');
      int second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFirst = rapiString.substring(first_blank_index + 1, second_blank_index);  // 2 digit year
      year = sFirst.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sSecond = rapiString.substring(second_blank_index + 1, first_blank_index); // month
      month = sSecond.toInt();
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sThird = rapiString.substring(first_blank_index + 1, second_blank_index);  // day  
      day = sThird.toInt();   
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sFourth = rapiString.substring(second_blank_index + 1, first_blank_index); // hour 
      hour = sFourth.toInt();    
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFifth = rapiString.substring(first_blank_index + 1, second_blank_index);  // min
      minutes = sFifth.toInt();
      sSixth = rapiString.substring(rapiString.lastIndexOf(' '),rapiString.indexOf('^'));    // sec   not used    
    }
  }
  s += "<FORM METHOD='get' ACTION='datetimeR'>";
  s += "<P><FONT FACE='Arial'><FONT SIZE=4>Current Date is ";
  s += "<SELECT name='month'><OPTION value='1'";
  if (month == 1)
    s += " SELECTED";
  s += " >January</OPTION><OPTION value='2'";
  if (month == 2)
   s += " SELECTED";
  s += " >February</OPTION><OPTION value='3'";
  if (month == 3)
   s += " SELECTED";
  s += " >March</OPTION><OPTION value='4'";
  if (month == 4)
   s += " SELECTED";
  s += " >April</OPTION><OPTION value='5'";
  if (month == 5)
   s += " SELECTED";
  s += " >May</OPTION><OPTION value='6'";
  if (month == 6)
   s += " SELECTED";
  s += " >June</OPTION><OPTION value='7'";
  if (month == 7)
   s += " SELECTED";
  s += " >July</OPTION><OPTION value='8'";
  if (month == 8)
   s += " SELECTED";
  s += " >August</OPTION><OPTION value='9'";
  if (month == 9)
   s += " SELECTED";
  s += " >September</OPTION><OPTION value='10'";
  if (month == 10)
   s += " SELECTED";
  s += " >October</OPTION><OPTION value='11'";
  if (month == 11)
   s += " SELECTED";
  s += " >November</OPTION><OPTION value='12'";
  if (month ==12)
   s += " SELECTED";
  s += " >December</OPTION></SELECT>";
  s += " <SELECT name='day'>";
  for (index = 1; index <= 31; index++){
     if (index == day)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>, 20";
  s += "<SELECT name='year'>";
  for (index = 16; index <= 99; index++){
     if (index == year)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "<P> Current Time (hh:mm) is ";
  s += "<SELECT name='hour'>";
  for (index = 0; index <= 9; index++){
     if (index == hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 23; index++){
     if (index == hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>:";
  s += "<SELECT name='minutes'>";
  for (index = 0; index <= 9; index++){
     if (index == minutes)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 59; index++){
     if (index == minutes)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM><FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleDateTimeR(){
  String s;
  String sMonth = server.arg("month");
  String sDay = server.arg("day");      
  String sYear = server.arg("year");
  String sHour = server.arg("hour");
  String sMinutes = server.arg("minutes");
  int month = 0;
  int day = 0;
  int year = 0;

  month = sMonth.toInt();
  year = sYear.toInt();
  switch (month){
    case 1:
      day = 31;
      break;
    case 2:
      if (year%4 == 0)
        day = 29;
      else
        day = 28;
      break;
    case 3:
      day = 31;
      break;
    case 4:
      day = 30;
      break;
    case 5:
      day = 31;
      break;
    case 6:
      day = 30;
      break;
    case 7:
      day = 31;
      break;
    case 8:
      day = 31;
      break;
    case 9:
      day = 30;
      break;
    case 10:
      day = 31;
      break;
    case 11:
      day = 30;
      break;
    case 12:
      day = 31;
      break;
    default:
      day = 0;
  }
  s = "<HTML><FONT SIZE=4><FONT FACE='Arial'>";
  if (sDay.toInt() <= day){
    String sCommand = "$S1 " + sYear + " " + sMonth + " " + sDay + " " + sHour + " " + sMinutes + " 0"; 
    Serial.flush();
    Serial.println(sCommand);
    delay(commDelay);   
    s += "Success!<P><FORM ACTION='home'>";
  }
  else
    s += "Invalid Date. Please try again.<P><FORM ACTION='datetime'>";  
  s += "<INPUT TYPE=SUBMIT VALUE='     OK     '></FORM></P></FONT></FONT></HTML>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleDelayTimer(){
  String s;
  // variables for command responses
  String sFirst = "0";
  String sSecond = "0";
  String sThird = "0";
  String sFourth = "0";
  String sFifth = "0";
  s = "<HTML>";
  s += "<h2>Set Delay Timer</h2>";  
 //get delay start timer
  delay(commDelay);
  Serial.flush();
  Serial.println("$GD^27");
  delay(commDelay);
  int start_hour = 0;
  int start_min = 0;
  int stop_hour = 0;
  int stop_min = 0;
  int index;
  while(Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      int first_blank_index = rapiString.indexOf(' ');
      int second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sFirst = rapiString.substring(first_blank_index + 1, second_blank_index);  // start hour
      start_hour = sFirst.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sSecond = rapiString.substring(second_blank_index + 1, first_blank_index); // start min
      start_min = sSecond.toInt();
      second_blank_index = rapiString.indexOf(' ',first_blank_index + 1);
      sThird = rapiString.substring(first_blank_index + 1, second_blank_index);  // stop hour
      stop_hour = sThird.toInt();
      first_blank_index = rapiString.indexOf(' ',second_blank_index + 1);
      sFourth = rapiString.substring(second_blank_index + 1, first_blank_index); // stop min
      stop_min = sFourth.toInt();
      sFifth = rapiString.substring(rapiString.lastIndexOf(' ') + 1,rapiString.indexOf('^'));  // timer enabled - not used    
    }
  }
  s += "<FORM METHOD='get' ACTION='delaytimerR'>";
  s += "<P><FONT FACE='Arial'><FONT SIZE=4>Start Time (hh:mm) - ";
  s += " <SELECT name='starthour'>";
  for (index = 0; index <= 9; index++){
     if (index == start_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
    for (index = 10; index <= 23; index++){
     if (index == start_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>:";
  s += "<SELECT name='startmin'>";
  for (index = 0; index <= 9; index++){
     if (index == start_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 59; index++){
     if (index == start_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "<P>Stop Timer (hh:mm) - ";
  s += "<SELECT name='stophour'>";
  for (index = 0; index <= 9; index++){
     if (index == stop_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 23; index++){
     if (index == stop_hour)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT>:";
  s += "<SELECT name='stopmin'>";
  for (index = 0; index <= 9; index++){
     if (index == stop_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + "0" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + "0" + String(index) + "</OPTION>";
  }
  for (index = 10; index <= 59; index++){
     if (index == stop_min)
       s += "<OPTION value='" + String(index) + "'SELECTED>" + String(index) + "</OPTION>";
     else
       s += "<OPTION value='" + String(index) + "'>" + String(index) + "</OPTION>";
  }
  s += "</SELECT></P>";
  s += "<P>Note. All zeros will turn OFF delay timer</P>";
  s += "&nbsp;<TABLE><TR>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Submit    '></TD>";
  s += "</FORM><FORM ACTION='home'>";
  s += "<TD><INPUT TYPE=SUBMIT VALUE='    Cancel    '></TD>";
  s += "</FORM>";
  s += "</TR></TABLE>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleDelayTimerR(){
  String s;
  String sStart_hour = server.arg("starthour");      
  String sStart_min = server.arg("startmin");
  String sStop_hour = server.arg("stophour");
  String sStop_min = server.arg("stopmin");       
  s = "<HTML>";
  String sCommand = "$ST " + sStart_hour + " "+ sStart_min + " " + sStop_hour + " " + sStop_min; 
  Serial.flush();
  Serial.println(sCommand);
  delay(commDelay);
  if ( (sStart_hour != "00") || (sStart_min != "00") || (sStop_hour != "00") || (sStop_min != "00")) //turn off timer
    sCommand = "$FS^31";     
  else
    sCommand = "$FE^27"; 
  Serial.flush();
  Serial.println(sCommand);
  delay(commDelay);  
  s += "<FORM ACTION='home'>";  
  s += "<P><FONT SIZE=4><FONT FACE='Arial'>Success!</P>";
  s += "<P><INPUT TYPE=SUBMIT VALUE='     OK     '></FONT></FONT></P>";
  s += "</FORM>";
  s += "</HTML>";
  s += "\r\n\r\n";
  server.send(200, "text/html", s);
}


void handleRapiRead() {
  Serial.flush(); 
  Serial.println("$GV*C1");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        firmware = rapiString.substring(firstRapiCmd, secondRapiCmd);
        protocol = rapiString.substring(secondRapiCmd);
      }
    }
  Serial.println("$GF*B1");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        int thirdRapiCmd = rapiString.indexOf(' ', secondRapiCmd+1);
        gfci_count = rapiString.substring(firstRapiCmd, secondRapiCmd);
        nognd_count = rapiString.substring(secondRapiCmd, thirdRapiCmd);
        stuck_count = rapiString.substring(thirdRapiCmd);
      }
    }
  Serial.println("$GC*AE");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        if (service == 1) {
          current_l1min = rapiString.substring(firstRapiCmd, secondRapiCmd);
          current_l1max = rapiString.substring(secondRapiCmd);
        }
        else {
          current_l2min = rapiString.substring(firstRapiCmd, secondRapiCmd);
          current_l2max = rapiString.substring(secondRapiCmd);
        }   
      }
    }
  Serial.println("$GA*AC");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        current_scale = rapiString.substring(firstRapiCmd, secondRapiCmd);
        current_offset = rapiString.substring(secondRapiCmd);
      }
    }
  Serial.println("$GH");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        kwh_limit = rapiString.substring(firstRapiCmd);
      }
    }
  Serial.println("$G3");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        time_limit = rapiString.substring(firstRapiCmd);
      }
    }
  Serial.println("$GU*C0");
  comm_sent++;
  delay(commDelay);
  while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        int firstRapiCmd = rapiString.indexOf(' ');
        int secondRapiCmd = rapiString.indexOf(' ', firstRapiCmd+1);
        wattsec = rapiString.substring(firstRapiCmd, secondRapiCmd);
        watthour_total = rapiString.substring(secondRapiCmd);
      }
    }
    Serial.println("$GE*B0");
     comm_sent++;
     delay(commDelay);
       while(Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           comm_success++;
           String qrapi; 
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
           last_datastr = "Pilot:";
           last_datastr += pilot;
           String flag = rapiString.substring(rapiString.lastIndexOf(' '));
           long flags = strtol(flag.c_str(), NULL, 16);
           service = bitRead(flags, 0) + 1;
           diode_ck = bitRead(flags, 1);
           vent_ck = bitRead(flags, 2);
           ground_ck = bitRead(flags, 3);
           stuck_relay = bitRead(flags, 4);
           auto_service = bitRead(flags, 5);
           auto_start = bitRead(flags, 6);
           serial_dbg = bitRead(flags, 7);
           rgb_lcd = bitRead(flags, 8);
           gfci_test = bitRead(flags, 9);
           temp_ck = bitRead(flags, 10);
         }
       }  
}

void setup() {
	delay(2000);
	Serial.begin(115200);
  EEPROM.begin(512);
  SPIFFS.begin(); // mount the fs
  pinMode(0, INPUT);
  espflash = ESP.getFlashChipSize();
  espvcc = ESP.getVcc();
  espfree = ESP.getFreeHeap();
  //char tmpStr[40];
  
 
  for (int i = 0; i < 32; ++i){
    char c = char(EEPROM.read(i));
    if (c!=0) esid += c;
  }
  for (int i = 32; i < 96; ++i){
    char c = char(EEPROM.read(i));
    if (c!=0) epass += c;
  }
  for (int i = 96; i < 128; ++i){
    char c = char(EEPROM.read(i));
    if (c!=0) apikey += c;
  }
  char c = char(EEPROM.read(129));
    if (c!=0) node += c;
  for (int i = 130; i < 138; ++i){
    char c = char(EEPROM.read(i));
    if (c!=0) ohm += c;
  }
     
  WiFi.disconnect();
  // 1) If no network configured start up access point
  if (esid == 0)
  {
    startAP();
    wifi_mode = 2; // AP mode with no SSID in EEPROM    
  } 
  // 2) else try and connect to the configured network
  else
  {
    WiFi.mode(WIFI_STA);
    wifi_mode = 0;
    startClient();
  }
  
 ArduinoOTA.begin();
  server.on("/", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleHome();
  });
  server.on("/r", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleRapiR();
  });
  server.on("/reset", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleRst();
  });
  server.on("/status", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleStatus();
  });
  server.on("/config", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleConfig();
  });
  server.on("/rapiupdate", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleUpdate();
  });
  server.on("/rapi", [](){
    if(!server.authenticate(www_username, www_password))
      return server.requestAuthentication();
    handleRapi();
  });
  server.on("/savenetwork", handleSaveNetwork);
  server.on("/saveapikey", handleSaveApikey);
  server.on("/saveohmkey", handleSaveOhmkey);
  server.on("/scan", handleScan);
  server.on("/apoff",handleAPOff);
  server.on("/datetime", handleDateTime);
  server.on("/datetimeR", handleDateTimeR);
  server.on("/delaytimer", handleDelayTimer);
  server.on("/delaytimerR", handleDelayTimerR);
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  
  httpUpdater.setup(&server);
	server.begin();
	//Serial.println("HTTP server started");
  Timer = millis();
  delay(5000); //gives OpenEVSE time to finish self test on cold start
  handleRapiRead();
}

void loop() {
ArduinoOTA.handle();
server.handleClient();
  
int erase = 0;  
buttonState = digitalRead(0);
while (buttonState == LOW) {
  buttonState = digitalRead(0);
  erase++;
  if (erase >= 5000) {
    ResetEEPROM();
    int erase = 0;
    WiFi.disconnect();
    Serial.print("Finished...");
    delay(2000);
    ESP.reset(); 
  } 
}
// Remain in AP mode for 5 Minutes before resetting
if (wifi_mode == 1){
   if ((millis() - Timer) >= 300000){
     ESP.reset();
   }
}   
if (wifi_mode == 0 || wifi_mode == 3 && ohm != 0){
  if ((millis() - Timer2) >= 60000){
     Timer2 = millis();
     WiFiClientSecure client;
     if (!client.connect(ohm_host, ohm_httpsPort)) {
       Serial.println("connection failed");
       return;
     }
     if (client.verify(ohm_fingerprint, ohm_host)) {
       client.print(String("GET ") + ohm_url + ohm + " HTTP/1.1\r\n" +
               "Host: " + ohm_host + "\r\n" +
               "User-Agent: OpenEVSE\r\n" +
               "Connection: close\r\n\r\n");
     String line = client.readString();
     if(line.indexOf("False") > 0) {
       //Serial.println("It is not an Ohm Hour");
       ohm_hour = "False";
       if (evse_sleep == 1){
         evse_sleep = 0;
         Serial.println("$FE*AF");
       }
     }
     if(line.indexOf("True") > 0) {
       //Serial.println("Ohm Hour");
       ohm_hour = "True";
       if (evse_sleep == 0){
         evse_sleep = 1;
         Serial.println("$FS*BD");
       }
     }
    //Serial.println(line);
  } 
  else {
    Serial.println("Certificate Invalid");
    }
  }
} 
if (wifi_mode == 0 || wifi_mode == 3 && apikey != 0){
   if ((millis() - Timer) >= 30000){
     Timer = millis();
     espvcc = ESP.getVcc();
     espfree = ESP.getFreeHeap();
     Serial.flush();
     Serial.println("$GE*B0");
     comm_sent++;
     delay(commDelay);
       while(Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           comm_success++;
           String qrapi; 
           qrapi = rapiString.substring(rapiString.indexOf(' '));
           pilot = qrapi.toInt();
         }
       }
     Serial.flush();
     Serial.println("$GS*BE");
     comm_sent++;
     delay(commDelay);
       while(Serial.available()) {
         String rapiString = Serial.readStringUntil('\r');
         if ( rapiString.startsWith("$OK ") ) {
           comm_success++; 
           String qrapi = rapiString.substring(rapiString.indexOf(' '));
           state = strtol(qrapi.c_str(), NULL, 16);
           if (state == 1) {
             estate = "Not_Connected";
           }
           if (state == 2) {
             estate = "EV_Connected";
           }
           if (state == 3) {
             estate = "Charging";
           }
           if (state == 4) {
             estate = "Vent_Required";
           }
           if (state == 5) {
             estate = "Diode_Check_Failed";
           } 
           if (state == 6) {
             estate = "GFCI_Fault";
           }
           if (state == 7) {
             estate = "No_Earth_Ground";
           }
           if (state == 8) {
             estate = "Stuck_Relay";
           }
           if (state == 9) {
             estate = "GFCI_Self_Test_Failed";
           }
           if (state == 10) {
             estate = "Over_Temperature";
           }
           if (state == 254) {
             estate = "Sleeping";
           }
           if (state == 255) {
             estate = "Disabled";
           }                 
           //last_datastr = ",State:";
           //last_datastr += estate;
         }
       }    
  
     delay(commDelay);
     Serial.flush();
     Serial.println("$GG*B2");
     comm_sent++;
     delay(commDelay);
     while(Serial.available()) {
       String rapiString = Serial.readStringUntil('\r');
       if ( rapiString.startsWith("$OK") ) {
         comm_success++;
         String qrapi; 
         qrapi = rapiString.substring(rapiString.indexOf(' '));
         amp = qrapi.toInt();
         String qrapi1;
         qrapi1 = rapiString.substring(rapiString.lastIndexOf(' '));
         volt = qrapi1.toInt();
       }
    }  
    Serial.flush(); 
    Serial.println("$GP*BB");
    comm_sent++;
    delay(commDelay);
    while(Serial.available()) {
      String rapiString = Serial.readStringUntil('\r');
      if (rapiString.startsWith("$OK") ) {
        comm_success++;
        String qrapi; 
        qrapi = rapiString.substring(rapiString.indexOf(' '));
        temp1 = qrapi.toInt();
        String qrapi1;
        int firstRapiCmd = rapiString.indexOf(' ');
        qrapi1 = rapiString.substring(rapiString.indexOf(' ', firstRapiCmd + 1 ));
        temp2 = qrapi1.toInt();
        String qrapi2;
        qrapi2 = rapiString.substring(rapiString.lastIndexOf(' '));
        temp3 = qrapi2.toInt();
      }
    } 
 
// Use WiFiClient class to create TCP connections
    WiFiClientSecure client;
    if (!client.connect(host, httpsPort)) {
      return;
    }
    
// Create the JSON String
  
  String s = e_url;
  s += String(node)+"&json={";
  s += "OpenEVSE_AMP:"+String(amp)+",";
  s += "OpenEVSE_TEMP1:"+String(temp1)+",";
  s += "OpenEVSE_TEMP2:"+String(temp2)+",";
  s += "OpenEVSE_TEMP3:"+String(temp3)+",";
  s += "OpenEVSE_PILOT:"+String(pilot)+",";
  s += "OpenEVSE_STATE:"+String(state);
  s += "}&devicekey="+String(apikey);
     
// Send the JSON request to the server
    
    packets_sent++;
    client.print(String("GET ") + s + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
    delay(10);
    String line = client.readString();
      if (line.indexOf("ok") >= 0){
          packets_success++;
        }
    
    //Serial.println(host);
    //Serial.println(url);
    
  }
}

}
