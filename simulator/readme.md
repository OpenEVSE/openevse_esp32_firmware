# OpenEVSE WiFi Simulator

Node web server app to simulate an OpenEVSE WiFi gateway, usually running on ESP8266 communicating with openevse controller via serial RAPI API

*This simulator is for demo/testing, to get a feel for the interface. Not all features have been implemented fully.*

## Setup

```
cd simulator
npm install
node app.js --port 80
```

**Note: currently the server has to be run on port 80 as the UI code doesn't currently handle running on a different port**
