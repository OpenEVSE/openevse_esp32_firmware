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

## Serve via apache


Install apache `mod-proxy` module then enable it:

```
sudo apt-get install libapache2-mod-proxy-html
sudo a2enmod proxy
sudo a2enmod proxy_http
sudo a2enmod rewrite
```

copy `example-openevse-apache.conf` to `/etc/apache2/sites-available` making the relevant changes for your server then enable the site using `a2ensite`. e.g.

```
sudo cp example-openevse-apache.conf /etc/apache2/sites-available/openevse.conf
sudo a2ensite openevse
```

Create log files, this step may not be needed but it's a good idea to check the permissions.

```
sudo touch /var/log/apache2/openevse_error.log
sudo touch /var/log/apache2/openevse_access.log
sudo service restart apache2
```
