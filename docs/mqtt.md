### MQTT

MQTT and MQTTS (secure) connections are supported for status and control.

At startup the following message is published with a retain flag to `openevse/announce/xxxx` where `xxxx` is the last 4 characters of the device ID. This message is useful for device discovery and contans the device hostname and IP address.

```json
{
  "state":"connected",
  "id":"c44f330dxxad",
  "name":"openevse-55ad",
  "mqtt":"emon/openevse-55ad",
  "http":"http://192.168.1.43/"
}
```

For device discovery you should subscribe with a wild card to `openevse/announce/#`

When the device disconnects from MQTT the same message is posted with `state":"disconnected"` (Last Will and Testament).

All subsequent MQTT status updates will by default be be posted to `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID. This base-topic can be changed via the MQTT service page.

#### OpenEVSE Status via MQTT

OpenEVSE can post its status values (e.g. amp, wh, temp1, temp2, temp3, pilot, status) to an MQTT server. Data will be published as a sub-topic of base topic, e.g `<base-topic>/amp`. Data is published to MQTT every 30s.

**The default `<base-topic>` is `openevse-xxxx` where `xxxx` is the last 4 characters of the device ID**

Controls:

`<base-topic>/divertmode/set`      : [1 (disable) | 2 (enable)] divert mode
`<base-topic>/max_current/set`     : [int in A] set max software current value
`<base-topic>/pilot/set`           : [in in A] override charge current/pilot. Use `<base-topic>/manual_override/set delete` to remove overrides.
`<base-topic>/manual_override/set` : [start / stop / delete] Manually enable / disable charge. Using delete remove the override.


MQTT setup is pre-populated with OpenEnergyMonitor [emonPi default MQTT server credentials](https://guide.openenergymonitor.org/technical/credentials/#mqtt).

* Enter MQTT server host and base-topic
* (Optional) Enter server authentication details if required
* Click connect
* After a few seconds `Connected: No` should change to `Connected: Yes` if connection is successful. Re-connection will be attempted every 10s. A refresh of the page may be needed.

*Note: `emon/xxxx` should be used as the base-topic if posting to emonPi MQTT server if you want the data to appear in emonPi Emoncms. See [emonPi MQTT docs](https://guide.openenergymonitor.org/technical/mqtt/).*
