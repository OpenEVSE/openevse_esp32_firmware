#include "ESP8266WiFi.h"
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <EEPROM.h>

MDNSResponder mdns;
WiFiServer server(80);


const char* ssid = "OpenEVSE";
const char* password = "openevse";
String st;

//Enter your Emoncms.org Write key here
const char* privateKey = "your_Write_API_key";


const char* host = "www.emoncms.org";
const char* inputID_AMP   = "OpenEVSE_AMP:";
const char* inputID_VOLT   = "OpenEVSE_VOLT:";
const char* inputID_TEMP1   = "OpenEVSE_TEMP1:";
const char* inputID_TEMP2   = "OpenEVSE_TEMP2:";
const char* inputID_TEMP3   = "OpenEVSE_TEMP3:";
const char* inputID_PILOT   = "OpenEVSE_PILOT:";

int amp = 0;
int volt = 240;
int temp1 = 10;
int temp2 = 0;
int temp3 = 0;
int pilot = 16;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  delay(10000);
Serial.println();
  Serial.println();
  delay(100);
  Serial.println("$FB 5");
  delay(100);
  Serial.println("$FP 0 0 ................");
  delay(100);
  Serial.println("$FP 0 1 ................");
  delay(100);
  Serial.println("OpenEVSE WiFi Startup");
  Serial.println("$FP 0 0 OpenEVSE.WiFi...");
  delay(100);
  Serial.println("$FP 0 1 Starting");
  delay(1000);
  // read eeprom for ssid and pass
  Serial.println("Reading EEPROM for SSID");
  String esid;
  for (int i = 0; i < 32; ++i)
    {
      esid += char(EEPROM.read(i));
    }
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM for PASSWORD");
  String epass = "";
  for (int i = 32; i < 96; ++i)
    {
      epass += char(EEPROM.read(i));
    }
  Serial.print("PASS: ");
  Serial.println("********");  
  if ( esid.length() > 1 ) {
      // test esid 
      WiFi.begin(esid.c_str(), epass.c_str());
      delay(100);
      Serial.println("$FP 0 0 Connecting.to...");
      delay(100);
      Serial.println("$FP 0 1 ................");
      delay(100);
      Serial.print("$FP 0 1 ");
      Serial.println(esid.c_str());
      delay(50);
   
      if ( testWifi() == 20 ) { 
          //launchWeb(0);
          Serial.println("$FP 0 0 OpenEVSE.WiFi...");
          delay(100);
          Serial.println("$FP 0 1 Connected.......");
          delay(100);
          return;
      }
  else {
    setupAP(); 
  }
  }
   //Serial.println("$FS*BD");
} 

int testWifi(void) {
  int c = 0;
  Serial.println("Waiting for Wifi to connect");  
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED) { return(20); } 
    delay(500);
    Serial.print(WiFi.status());    
    c++;
  }
  Serial.println("Connect timed out, opening AP");
  return(10);
} 

void launchWeb(int webtype) {
          Serial.println("");
          Serial.println("WiFi connected");
          Serial.println(WiFi.localIP());
          Serial.println(WiFi.softAPIP());
          if (!mdns.begin("esp8266", WiFi.localIP())) {
            Serial.println("Error setting up MDNS responder!");
            while(1) { 
              delay(1000);
            }
          }
          Serial.println("mDNS responder started");
          // Start the server
          server.begin();
          Serial.println("Server started");   
          int b = 20;
          int c = 0;
          while(b == 20) { 
             b = mdns1(webtype);
           }
}

void setupAP(void) {
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
     {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.println(")");
      //Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
      delay(10);
     }
  }
  Serial.println(""); 
  st = "<ul>";
  for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      st += "<li>";
      //st +=i + 1;
      //st += ": ";
      st += WiFi.SSID(i);
      //st += " (";
      //st += WiFi.RSSI(i);
      //st += ")";
      //st += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
      st += "</li>";
    }
  st += "</ul>";
  delay(100);
  WiFi.softAP(ssid, password);
  Serial.println("softap");
  Serial.println("");
  delay(100);
  Serial.println("$FP 0 0 SSID...OpenEVSE.");
  delay(100);
  Serial.println("$FP 0 1 PASS...openevse.");
  delay(5000);
  Serial.println("$FP 0 0 IP_Address......");
  delay(100);
  Serial.print("$FP 0 1 ");
  Serial.print(WiFi.softAPIP());
  Serial.println(".....");
  delay(50); 
  launchWeb(1);
  Serial.println("over");
}

int mdns1(int webtype)
{
  // Check for any mDNS queries and send responses
  mdns.update();
  
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return(20);
  }
  Serial.println("");
  Serial.println("New client");
  

  // Wait for data from client to become available
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
    Serial.print("Invalid request: ");
    Serial.println(req);
    return(20);
   }
  req = req.substring(addr_start + 1, addr_end);
  Serial.print("Request: ");
  Serial.println(req);
  client.flush(); 
  String s;
  if ( webtype == 1 ) {
      if (req == "/")
      {
        IPAddress ip = WiFi.softAPIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Welcome to OpenEVSE Wifi Configuration";
        //s += ipStr;
        s += "<p>";
        s += st;
        s += "<form method='get' action='a'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/a?ssid=") ) {
        // /a?ssid=blahhhh&pass=poooo
        Serial.println("clearing eeprom");
        for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }
        String qsid; 
        qsid = req.substring(8,req.indexOf('&'));
        Serial.println(qsid);
        Serial.println("");
        String qpass;
        qpass = req.substring(req.lastIndexOf('=')+1);
        qpass.replace("%23", "#");
        Serial.println(qpass);
        Serial.println("");
        
        Serial.println("Writing SSID to Memory:");
        for (int i = 0; i < qsid.length(); ++i)
          {
            EEPROM.write(i, qsid[i]);
            Serial.print("Wrote: ");
            Serial.println(qsid[i]); 
          }
        Serial.println("Writing Password to Memory:"); 
        for (int i = 0; i < qpass.length(); ++i)
          {
            EEPROM.write(32+i, qpass[i]);
            Serial.print("Wrote: ");
            Serial.println("*"); 
          }    
        EEPROM.commit();
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>SSID and Password";
        //s += req;
        s += "<p> Saved to Memory... Please reset to join your network</html>\r\n\r\n";
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }
  } 
  else
  {
      if (req == "/")
      {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>OpenEVSE WiFi Configuration";
        s += "<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/reset") ) {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>OpenEVSE Wifi Reset";
        s += "<p>Clearing the EEPROM<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");  
        Serial.println("Clearing Memory");
        for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }
        EEPROM.commit();
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }       
  }
  client.print(s);
  Serial.println("Done with client");
  return(20);
}

void loop() {
  
  delay(5000);
  ++amp;
  ++temp1;
//OpenEVSE RAPI
// Get pilot setting $GE*B0
// Get amps and volts $GG*B2
// Get Temps $GP*BB

 Serial.println("$GE*B0");
 delay(10);
 while(Serial.available()) {
   String rapiString = Serial.readStringUntil('\r');
   //String delims = " ";
   //String tokens = rapiString.split(delims);
   }
 //int pilot = int(parseInt(tokens[1]));

 
 Serial.println("$GG*B2");
 delay(10);
 /*
 while(Serial.available()) {
   String rapiString = Serial.readStringUntil('\r');
   String delims = "[ ]";
   String[] tokens = rapiString.split(delims);
   }
int amp = Integer.parseInt(tokens[1]);
int volt = Integer.parseInt(tokens[2]);
   
 Serial.println("$GP*BB");
 delay(10);
 while(Serial.available()) {
   String rapiString = Serial.readStringUntil('\r');
   String delims = "[ ]";
   String[] tokens = rapiString.split(delims);
   }
int temp1 = Integer.parseInt(tokens[1]);
int temp2 = Integer.parseInt(tokens[2]);
int temp3 = Integer.parseInt(tokens[3]);
*/
// Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return;
  }
  
// We now create a URL for OpenEVSE RAPI data upload request
  String url = "/input/post.json?json={";
  String url_amp = inputID_AMP;
    url_amp += amp;
    url_amp += ",";
  String url_volt = inputID_VOLT;
    url_volt += volt;
    url_volt += ",";
  String url_temp1 = inputID_TEMP1;
    url_temp1 += temp1;
    url_temp1 += ",";
  String url_temp2 = inputID_TEMP2;
    url_temp2 += temp2;
    url_temp2 += ","; 
  String url_temp3 = inputID_TEMP3;
    url_temp3 += temp3;
    url_temp3 += ","; 
  String url_pilot = inputID_PILOT;
    url_pilot += pilot;
  
  url += url_amp;
  if (volt != 0) {
    url += url_volt;
    }
  if (temp1 != 0) {
    url += url_temp1;
    }
  if (temp2 != 0) {
    url += url_temp2;
    }
  if (temp3 != 0) {
    url += url_temp3;
    }
  url += url_pilot;
  url += "}&apikey=";
  url += privateKey;
    
// This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" + 
               "Connection: close\r\n\r\n");
  delay(10);
  
 // udp.beginPacketMulticast(addr, port, WiFi.localIP())
 // udp.write(ReplyBuffer);
 //   udp.endPacket();
  
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
    }
    
  
  Serial.print("connecting to ");
  Serial.println(host);
  Serial.print("Requesting URL: ");
  Serial.println(url);
  Serial.println();
  Serial.println("closing connection");
 
}

