# OpenEVSE WiFi Simulator

Node web server app to simulate an OpenEVSE WiFi gateway, usually running on ESP8266 communicating with openevse controller via serial RAPI API

*This simulator is for demo/testing, to get a feel for the interface. Not all features have been implemented fully.*

## Requirements

```
sudo apt-get intall node nodejs npm
```


## Setup

```
cd simulator
npm install
node app.js --port 3000
```

Then point your browser at http://localhost:3000/

**Tip**
The OpenEVSE WiFi HTML/JS/CSS can be 'compiled' without building the full firmware using the command:
```
pio run -t buildfs
```
