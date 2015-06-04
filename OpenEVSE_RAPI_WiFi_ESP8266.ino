#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

#define WIFI_DEBUG

// Enter your Wifi SSOD and Password Here
const char* ssid     = "your_ssid";
const char* password = "your_password";

//Enter your Emoncms.org Write key here
const char* privateKey = "your_Write_API_Key";


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

void handle_root() {
  server.send(200, "text/plain", "OpenEVSE Amp = " + String(amp) + " OpenEVSE Volt = " + String(volt));
  
}

void setup() {
  Serial.begin(115200);
  delay(10000);

#ifdef WIFI_DEBUG
  Serial.print("Connecting to ");
  Serial.println(ssid);
  #endif
  
  //Serial.println("$FS*BD");
  delay(100);
  Serial.println("$FB 5");
  delay(100);
  Serial.println("$FP 0 0 ................");
  delay(100);
  Serial.println("$FP 0 1 ................");
  delay(100);
  Serial.println("$FP 0 0 Connecting.to...");
  delay(100);
  Serial.println("$FP 0 1 " + String(ssid));
  delay(1000);

  
//Connect to WIFI  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    }

server.on("/", handle_root);
server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });
  
  Serial.println("$FP 0 0 WiFi.Connected..");
  delay(100);
  Serial.println("$FP 0 1 ................");
  delay(1000);

  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  
  delay(100);
  Serial.println("$FP 0 0 IP_Address......");
  delay(100);
  Serial.print("$FP 0 1 ");
  Serial.println(WiFi.localIP());
  delay(5000); 
 
 server.begin();
  Serial.println("HTTP server started");
  delay(100);
  Serial.println("$FP 0 0 Web_Server......");
  delay(100);
  Serial.println("$FP 0 1 Started.........");
  delay(1000); 
  Serial.println("$FP 0 0 Internet.link...");
  delay(100);
  Serial.println("$FP 0 1 " + String(host));
  delay(100); 
  Serial.print("$FE*AF");
}

void loop() {
  server.handleClient();
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

