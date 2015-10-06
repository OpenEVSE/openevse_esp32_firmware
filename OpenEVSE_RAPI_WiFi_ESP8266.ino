// -*- C++ -*-
/*
 * Copyright (c) 2015 Chris Howell
 * Copyright (c) 2015 Sam C. Lin
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
#include "ESP8266WiFi.h"
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <EEPROM.h>


//#define DBG

#define CLIENT_TIMEOUT 300000

const char* ssid = "OpenEVSE";
const char* password = "openevse";
String st;
String privateKey = "";
String node = "";

const char* host = "data.openevse.com";
const char *uri = "/emoncms/input/post.json?";
const char* inputID_AMP   = "OpenEVSE_AMP:";
const char* inputID_VOLT   = "OpenEVSE_VOLT:";
const char* inputID_TEMP1   = "OpenEVSE_TEMP1:";
const char* inputID_TEMP2   = "OpenEVSE_TEMP2:";
const char* inputID_TEMP3   = "OpenEVSE_TEMP3:";
const char* inputID_PILOT   = "OpenEVSE_PILOT:";


int amp = 0;
int32_t volt = 0;
int temp1 = 0;
int temp2 = 0;
int temp3 = 0;
int pilot = 0;

int buttonState = 0;
int clientTimeout = 0;

/* For Serial Debug
inline void dbgprint(const char *s) {
  Serial.print(s);
}
inline void dbgprintln(const char *s) {
  Serial.println(s);
}
*/

#ifdef DBG
#define dbgprint(s) Serial.print(s)
#define dbgprintln(s) Serial.println(s)
#else
#define dbgprint(s) 
#define dbgprintln(s) 
#endif // DBG

#define RAPI_TIMEOUT_MS 500
#define RAPI_BUFLEN 40
#define RAPI_MAX_TOKENS 10
class RapiSerial {
  char respBuf[RAPI_BUFLEN];
  void tokenize();
  void _sendCmd(const char *cmdstr);
public:
  int tokenCnt;
  char *tokens[RAPI_MAX_TOKENS];

  RapiSerial() {*respBuf = 0;}
  void sendString(const char *str) { dbgprint(str); }
  int sendCmd(const char *cmdstr);
};

void RapiSerial::_sendCmd(const char *cmdstr)
{
  Serial.print(cmdstr);
  const char *s = cmdstr;
  uint8_t chk = 0;
  while (*s) {
    chk ^= *(s++);
  }
  sprintf(respBuf,"^%02X\r",(unsigned)chk);
  Serial.print(respBuf);
  Serial.flush();

  *respBuf = 0;
}


void RapiSerial::tokenize()
{
  char *s = respBuf;
  while (*s) {
    tokens[tokenCnt++] = s++;
    if (tokenCnt == RAPI_MAX_TOKENS) break;
    while (*s && (*s != ' ')) s++;
    if (*s == ' ') *(s++) = '\0';
  }
}

/*
 * return values:
 * -1= timeout
 * 0= success
 * 1=$NK
 * 2=invalid RAPI response
*/
int RapiSerial::sendCmd(const char *cmdstr)
{
 start:
  tokenCnt = 0;
  *respBuf = 0;

  int rc;
  _sendCmd(cmdstr);
  int bufpos = 0;
  unsigned long mss = millis();
  do {
    int bytesavail = Serial.available();
    if (bytesavail) {
      for (int i=0;i < bytesavail;i++) {
	char c = Serial.read();

	if (!bufpos && c != '$') {
	  // wait for start character
	  continue;
	}
	else if (c == '\r') {
	  respBuf[bufpos] = '\0';
	  tokenize();
	  
	}
	else {
	  respBuf[bufpos++] = c;
	  if (bufpos >= (RAPI_BUFLEN-1)) return -2;
	}
      }
    }
  } while (!tokenCnt && ((millis() - mss) < RAPI_TIMEOUT_MS));
  
/*  
    dbgprint("\n\rTOKENCNT: ");dbgprintln(tokenCnt);
    for (int i=0;i < tokenCnt;i++) {
      dbgprintln(tokens[i]);
    }
    dbgprintln("");
*/
  

  if (tokenCnt) {
    if (!strcmp(respBuf,"$OK")) {
      return 0;
    }
    else if (!strcmp(respBuf,"$NK")) {
      return 1;
    }
    else if (!strcmp(respBuf,"$WF")) { // async WIFI
      ResetEEPROM();
      goto start;
    }
    else if (!strcmp(respBuf,"$ST")) { // async EVSE state transition
      // placeholder.. no action et
      goto start;
    }
    else {
      return 2;
    }
  }
  else {
    return -1;
  }
}

MDNSResponder mdns;
WiFiServer server(80);
RapiSerial rapi;
char tmpStr[40];

void ResetEEPROM()
{
  dbgprintln("Erasing EEPROM");
  for (int i = 0; i < 512; ++i) { 
    EEPROM.write(i, 0);
    //Always print this so user can see activity during button reset
    Serial.print("#"); 
  }
  EEPROM.commit();   
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(0, INPUT);
  // read eeprom for ssid and pass
  String esid;
  for (int i = 0; i < 32; ++i)
    {
      esid += char(EEPROM.read(i));
    }
  String epass = "";
  for (int i = 32; i < 96; ++i)
    {
      epass += char(EEPROM.read(i));
    }
  for (int i = 96; i < 128; ++i)
    {
      privateKey += char(EEPROM.read(i));
    }
  node += char(EEPROM.read(129));
  
  dbgprint("\nesid:");dbgprint(esid.c_str());dbgprintln("-");
  dbgprint("epass:");dbgprint(epass.c_str());dbgprintln("-");
  
  if ( esid.length() > 1 ) { 
    if (WiFi.status() != WL_CONNECTED){
      dbgprintln("esid");
      // test esid
      WiFi.mode(WIFI_STA);
      dbgprintln("esid0");
      WiFi.disconnect(); 
      dbgprintln("esid1");
      WiFi.begin(esid.c_str(), epass.c_str());
      dbgprintln("esid2");
      delay(50);
      dbgprintln("esid3");
    }
    if ( testWifi() == 20 ) { 
      //launchWeb(0);
      return;
    }
    else {
      setupAP(); 
    }
  }
  else {
    setupAP(); 
  }
} 

int testWifi(void) {
  int c = 0;
  dbgprintln("Waiting for Wifi to connect");  
  while ( c < 30 ) {
    if (WiFi.status() == WL_CONNECTED) { return(20); } 
    delay(500);
    dbgprintln(WiFi.status());    
    c++;
  }
  return(10);
} 

void launchWeb(int webtype) {
          dbgprintln(WiFi.localIP());
          dbgprintln(WiFi.softAPIP());
          if (!mdns.begin("esp8266", WiFi.localIP())) {
            dbgprintln("Error setting up MDNS responder!");
            while(1) { 
              delay(1000);
            }
          }
          server.begin();
          dbgprintln("Server started");   
          int b = 20;
          int c = 0;
          while(b == 20) { 
             b = mdns1(webtype);
           }
}

void setupAP(void) {
  dbgprintln("setupap");
  WiFi.mode(WIFI_STA);
  dbgprintln("su1");
  WiFi.disconnect();
  dbgprintln("su2");
  delay(100);
  int n = WiFi.scanNetworks();
  dbgprintln("su3");
    dbgprint(n);
    dbgprintln(" networks found");
    
  st = "<ul>";
  for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      st += "<li>";
      st += WiFi.SSID(i);
      st += "</li>";
    }
  st += "</ul>";
  delay(100);
  WiFi.softAP(ssid, password);
  dbgprintln("Access Point Mode");
  dbgprintln("");
  delay(100);
#ifdef DBG
  rapi.sendCmd("$FP 0 0 SSID...OpenEVSE.");
  delay(100);
  rapi.sendCmd("$FP 0 1 PASS...openevse.");
  delay(5000);
  rapi.sendCmd("$FP 0 0 IP_Address......");
  delay(100);
  IPAddress ip = WiFi.softAPIP();
  sprintf(tmpStr,"$FP 0 1 %d.%d.%d.%d",ip[0],ip[1],ip[2],ip[3]);
  rapi.sendCmd(tmpStr);
  dbgprintln(".....");
  delay(50); 
#endif // DBG
  launchWeb(1);
}

int mdns1(int webtype)
{
  // Check for any mDNS queries and send responses
  mdns.update();
  
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    delay(1);
    clientTimeout++;
    if (clientTimeout >= CLIENT_TIMEOUT){
     dbgprintln("Client timeout - Rebooting");
      //ESP.deepSleep(10000, WAKE_RF_DEFAULT);
      ESP.reset();
    }
    return(20);
  }
  // Wait for data from client to become available
  int i = 0;
  while(client.connected() && !client.available()){
    delay(1);
    }
  
  // Read the first line of HTTP request
  String req = client.readStringUntil('\r');
  
  // First line of HTTP request looks like "GET /path HTTP/1.1"
  // Retrieve the "/path" part by finding the spaces
  int addr_start = req.indexOf(' ');
  int addr_end = req.indexOf(' ', addr_start + 1);
  if (addr_start == -1 || addr_end == -1) {
    dbgprint("Invalid request: ");
    dbgprintln(req);
    return(20);
   }
  req = req.substring(addr_start + 1, addr_end);
  client.flush(); 
  String s;
  if ( webtype == 1 ) {
      if (req == "/")
      {
        IPAddress ip = WiFi.softAPIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>Networks Found:<p>";
        //s += ipStr;
        s += "<p>";
        s += st;
        s += "<p>";
        s += "<form method='get' action='a'><label><b><i>WiFi SSID:</b></i></label><input name='ssid' length=32><p><label><b><i>Password  :</b></i></label><input name='pass' length=64><p><label><b><i>Emon Key:</b></i></label><input name='ekey' length=32><p><label><b><i>OpenEVSE:</b></i></label><select name='node'><option value='0'>0 - Default</option><option value='1'>1</option><option value='2'>2</option><option value='3'>3</option><option value='4'>4</option><option value='5'>5</option><option value='6'>6</option><option value='7'>7</option><option value='8'>8</option></select><p><input type='submit'></form>";
        s += "</html>\r\n\r\n";
        client.print(s);
      }
      else if ( req.startsWith("/a?ssid=") ) {
        // /a?ssid=blahhhh&pass=poooo
        ResetEEPROM();
        
        String qsid;
        String qpass;
        String qkey;
        String qnode; 
        
        int idx = req.indexOf("ssid=");
        int idx1 = req.indexOf("&pass=");
        int idx2 = req.indexOf("&ekey=");
        int idx3 = req.indexOf("&node=");
        
        qsid = req.substring(idx+5,idx1);
        qsid.replace("%23", "#");
        qsid.replace('+', ' ');
	qsid.trim();
        qpass = req.substring(idx1+6,idx2);
        qpass.replace("%23", "#");
        qpass.replace('+', ' ');
	qpass.trim();
        qkey = req.substring(idx2+6, idx3);
        qnode = req.substring(idx3+6);
        
        
        dbgprintln(qsid);
        dbgprintln("");
	
        for (int i = 0; i < qsid.length(); ++i)
          {
            EEPROM.write(i, qsid[i]);
            dbgprint("Wrote: ");
            dbgprintln(qsid[i]); 
          }
        dbgprintln("Writing Password to Memory:"); 
        for (int i = 0; i < qpass.length(); ++i)
          {
            EEPROM.write(32+i, qpass[i]);
            dbgprint("Wrote: ");
            dbgprintln("*"); 
          }
      dbgprintln("Writing EMON Key to Memory:"); 
        for (int i = 0; i < qkey.length(); ++i)
          {
            EEPROM.write(96+i, qkey[i]);
            dbgprint("Wrote: ");
            dbgprintln(qkey[i]); 
          }
      dbgprintln("Writing EMOM Node to Memory:"); 
            EEPROM.write(129, qnode[i]);
            dbgprint("Wrote: ");
            dbgprintln(qnode[i]); 
                 
        EEPROM.commit();
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>SSID and Password<p>";
        //s += req;
        s += "<p>Saved to Memory...<p>Wifi will reset to join your network</html>\r\n\r\n";
        client.print(s);
        delay(2000);
        WiFi.disconnect();
        ESP.reset();
      }
      else if ( req.startsWith("/reset") ) {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>Reset to Defaults:<p>";
        s += "<p><b>Clearing the EEPROM</b><p>";
        s += "</html>\r\n\r\n";
        ResetEEPROM();
        EEPROM.commit();
        client.print(s);
        WiFi.disconnect();
        delay(1000);
        ESP.reset();
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        client.print(s);
       }    
  }
  
  return(20);
}


// convert decimal string to int32_t
int32_t dtoi32(const char *s)
{
  int32_t i = 0;
  int8 sign;
  if (*s == '-') {
    sign = -1;
    s++;
  }
  else {
    sign = 0;
  }
  while (*s) {
    i *= 10;
    i += *(s++) - '0';
  }

  if (sign < 0) i = -i;
  return i;
}




void loop() {
int erase = 0;  
buttonState = digitalRead(0);
while (buttonState == LOW) {
    buttonState = digitalRead(0);
    erase++;
    if (erase >= 1000) {
        ResetEEPROM();
        int erase = 0;
        WiFi.disconnect();
        dbgprintln("Finished...");
        delay(1000);
        ESP.reset(); 
     } 
   }


 if (!rapi.sendCmd("$GE^26") && (rapi.tokenCnt == 3)) {
   pilot = atoi(rapi.tokens[1]);
 }   

 if (!rapi.sendCmd("$GG^24") && (rapi.tokenCnt == 3)) {
   amp = atoi(rapi.tokens[1]);
   volt = dtoi32(rapi.tokens[2]);
   if (volt < 0) volt = 0;
 }
 
 if (!rapi.sendCmd("$GP^33") && (rapi.tokenCnt == 4)) {
   temp1 = atoi(rapi.tokens[1]);
   temp2 = atoi(rapi.tokens[2]);
   temp3 = atoi(rapi.tokens[3]);
 }
 
 
// Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    dbgprintln("connect failed");
    return;
  }
  
// We now create a URL for OpenEVSE RAPI data upload request
  char req[300];
  sprintf(req,"GET %snode=%s&apikey=%s&json={",uri,node.c_str(),privateKey.c_str());
  sprintf(req+strlen(req),"%s%d,%s%d",inputID_AMP,amp,inputID_PILOT,pilot);
   if (volt >= 0) {
    sprintf(req+strlen(req),",%s%lu",inputID_VOLT,volt);
   }
  if (temp1 > 0) {
    sprintf(req+strlen(req),",%s%d",inputID_TEMP1,temp1);
  }
  if (temp2 > 0) {
    sprintf(req+strlen(req),",%s%d",inputID_TEMP2,temp2);
  }
  if (temp3 > 0) {
    sprintf(req+strlen(req),",%s%d",inputID_TEMP3,temp3);
  }
  sprintf(req+strlen(req),"} HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",host);
    
  dbgprintln(req);
  int prc = client.print(req);
  dbgprint("prc:");dbgprintln(prc);

  delay(10);
 
 
  while(client.available()){
    String line = client.readStringUntil('\r');
    }
    
  
  
  //  ESP.deepSleep(25000000, WAKE_RF_DEFAULT);
  delay(25000);
}

