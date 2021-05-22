# Tesla Login

Node web app to wrap logging in to the Tesla API to support OpenEVSE.

## Requirements

```
sudo apt-get intall node nodejs npm
```

Tested with `npm V5.6.0` and nodejs `v9.5.0`.

If a new version of nodejs is not available for your distribution you may need to update, [see nodejs install page](https://nodejs.org/en/download/package-manager/#debian-and-ubuntu-based-linux-distributions).


## Setup

```bash
cd tesla_login
npm install
node app.js --port 3000
```

Then point your browser at http://localhost:3000/

Depending on your npm setup you may need to install the following:

```bash
npm install body-parser
npm install express
npm install
```

## Run as a service

### Using systemd

`sudo cp tesla_login.service /etc/systemd/system/tesla_login.service`

Edit service file to specify correct path to match installation location

`sudo nano /etc/systemd/system/tesla_login.service`

Run at startup:

```bash
sudo systemctl daemon-reload
sudo systemctl enable tesla_login.service
```

### Using PM2

```
sudo npm install -g pm2
pm2 start app.js
```

For status:

```bash
pm2 info app
pm2 list
pm2 restart app
mp2 stop app
```

## Serve via apache

Install apache `mod-proxy` module then enable it:

```bash
sudo apt-get install libapache2-mod-proxy-html
sudo a2enmod proxy
sudo a2enmod proxy_http
sudo a2enmod rewrite
```

copy `example-tesla_login-apache.conf` to `/etc/apache2/sites-available` making the relevant changes for your server then enable the site using `a2ensite`. e.g.

```bash
sudo cp example-tesla_login-apache.conf /etc/apache2/sites-available/tesla_login.conf
sudo a2ensite tesla_login
```

Create log files, this step may not be needed but it's a good idea to check the permissions.

```bash
sudo touch /var/log/apache2/openevse_error.log
sudo touch /var/log/apache2/openevse_access.log
sudo service restart apache2
```
