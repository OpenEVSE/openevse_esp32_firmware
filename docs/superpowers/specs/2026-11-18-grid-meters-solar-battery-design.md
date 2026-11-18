# Unify grid meter and solar inputs

**Status:** seeking agreement. Not an implementation plan, and not scheduled.

## The ask

Grid power, solar production, voltage, and home-battery readings should be measurements the charger owns, not settings buried inside the feature that happens to use them.

MQTT topic names and the fields on POST /status stay the same. A client that posts solar, grid, shaper power, voltage, or home-battery values today keeps posting the same JSON. A post is applied when HTTP is the source of that measurement. Setups that today mix HTTP with MQTT or Shelly for the same reading will need to choose one.

## What is wrong today

With Shelly off, set the shaper topic to the same text as the grid topic, switch Eco to solar production, and restart. The shaper stops receiving power readings, and after two minutes it pauses the charge. The settings page still shows the topic.

The same reading is configured in more than one place, and one source replaces another without the settings page saying so. Grid power and solar live on Self-production, voltage lives on the MQTT page, shaper power lives on the load shaper, and Shelly is its own page that reaches into the other three. Three of the replacements are described by hiding the topic and saying "Supplied by Shelly LNM while it is enabled." One of those three is wrong. The rest are invisible.

| Scenario | What wins | What loses | What the screen says |
|---|---|---|---|
| Shelly is off. The shaper topic is the same text as the grid topic. Eco is switched to solar production and the charger is restarted. | Nobody | The shaper | The page still shows the topic. After two minutes the shaper pauses the charge. |
| Solar and grid share one MQTT topic. Eco is in grid excess, and a message arrives on that topic. | Solar | Grid excess | Nothing. The message is stored as solar and never as grid, so Eco never sees the sample. [#1413](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1413) includes a fix for this. |
| One POST /status contains both a solar value and a grid value. Eco is in grid excess. | Solar | The grid value in that post | Nothing. Solar is applied and grid is dropped. [#1409](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/1409), fix in [#1410](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1410). |
| Grid power is arriving by MQTT. An HTTP post sends a new grid value, then a Shelly packet arrives. The same last write happens for solar, shaper power, and home battery. | Whichever of HTTP, MQTT, or Shelly wrote last | The previous value | Nothing. Vehicle posts are refused unless the vehicle source is HTTP. These are not. Home battery shows the last value with no source. |
| Shelly is on. The shaper topic is blank, or it is the same text as the grid topic. | Shelly live power | The shaper MQTT topic | The shaper page hides the topic and says Shelly supplies it. A distinct shaper topic is still hidden, and Shelly does not take that topic. |
| Shelly is on and Eco is in grid excess, with a grid MQTT topic also set. An HTTP post then sends a grid value. | Shelly, until that post | The grid MQTT topic, then Shelly | Self-production replaces the topic with the Shelly sentence. It does not say the later post overwrites Shelly. |
| Shelly is off and Eco is in grid excess. The shaper topic is the same text as the grid topic, the solar topic is different, and a grid message arrives. | The grid message | A separate subscription of the shaper topic | Nothing. The shaper topic is not subscribed on its own; the grid message feeds the shaper. The two fields look independent. If the solar topic is that same text too, the solar handler runs first and the grid handler updates neither grid power nor shaper power. [#1413](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1413) changes that path in grid mode. |

The same pattern in voltage, the unselected Eco feed, Shelly's own discarding of samples, and subscriptions that follow the mode only when the broker connects, is in the [appendix](#appendix-other-silent-replacements).

In solar-production mode the shaper also adds solar watts to its power cap, so a solar sample changes the cap. The shaper page does not mention solar.

## What we would be agreeing to

**One page for the inputs.** Grid meter (power and voltage), solar production, and home battery. Shelly's address, port, device, and field names live under the grid meter, because Shelly is one way to fill that meter. The MQTT broker stays on the MQTT page. The voltage topic moves off it.

**One source per measurement, and it is exclusive.** Chosen from a dropdown, as vehicle data already is. Choosing MQTT means Shelly packets and HTTP posts for that measurement are ignored. Choosing HTTP means MQTT and Shelly samples are not applied to that measurement. A topic stays subscribed when another measurement whose source is MQTT still uses it. Choosing Shelly means it fills grid power and voltage only. Choosing none means the measurement is unused. Topics and other values entered for the choices that are not selected are kept, so switching away and back does not mean typing them again. A grid meter on Shelly and a separate shaper meter on HTTP is a normal setup: exclusive is per measurement, not one transport for the whole charger.

**Features subscribe.** This part is new. Vehicle data does not work this way: the winning writer stores the value, and the dashboard reads it. Eco, the shaper, the displayed voltage, and the energy log ask for the measurement they need, and a new sample is pushed to them.

**The screen says what happened.** On the input page, a measurement says which features are reading it. The shaper page names the source of the number it is using: "Source: MQTT", "Source: HTTP Post", or "None" when nothing is configured. When MQTT is only supplying solar, and the shaper is adding those watts to its cap, that line says "Max+Solar". The value and its age are shown there too. Self-production and the charger voltage page name their source the same way. Shelly's 5 second minimum interval and its 1 V voltage step are stated on the Shelly block.

**Existing separate HTTP posts keep working.** A client that only posts shaper power, and never set an MQTT topic, still has a shaper meter whose source is HTTP.

**The shaper reads the grid meter by default.** Whether a per-shaper override stays is the open point below.

Left where they are: vehicle data, the static charger voltage, EmonCMS and other outputs, the three-phase option, and Eco and shaper tuning.

An earlier Home Assistant sketch would add another source selector on the Self-production page and another on the shaper page. This proposal goes the other way: one source per measurement.

## What this makes easier

[#1404](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/1404) wants, after a charge, how much of the session came from solar surplus and how much from grid import. That is a reader of the grid meter. It should not care whether the meter was Shelly, MQTT, or HTTP, and it should still see the meter when Eco is following solar production. Today the grid feed is not even subscribed in that mode.

The split belongs in the energy log, on the same session watt-hours the charger already records, so the parts add up to the session total. A missing or stale grid meter is "not attributed", not "100% solar". Solar production is generation, not energy that entered the car. When home-battery power was reported during the session, the breakdown is grid, solar, and battery. Export is not labelled solar while the battery is discharging. Without a battery reading there is no battery figure. Grid and solar are only the measured parts. Any of the session those two do not account for stays unattributed, and is not called solar. A price per kilowatt-hour waits until there is a rule for it.

Session history and the day, week, month and year totals store one energy figure. They should gain the same grid/solar/battery split, while the minute history keeps a single total so existing data isn't lost.

## Testing

The simulator should be able to feed each source, and to show that a sample from a source that does not own the measurement is ignored. It should not need a broker or a Shelly on the network to do that. Today the simulator writes solar and grid straight into Eco, so the question of which source won never comes up.

Traces we already replay stay. As part of this new testing, new CSV files would be supplied with 1-second meter and PV recordings. A one-second interval is what makes the throttling and the input filter testable. The traces we replay today are a minute or more apart, which is too coarse to see either. One representative day is enough to check in. The sign of that meter has to be set explicitly when it does not already match the charger's import-positive convention.

The same run should be able to show the session split, so "No waste" and "No import" can be compared on one trace, including a day from those new recordings.

## Not in this discussion

How the firmware is structured, what the new settings are called, how old configs are migrated, and how the CSV loader accepts the new recordings. Those follow if the direction is agreed.

Also out of scope: tariff cost, Shelly as a solar source, a second Shelly, per-phase measurements, and Home Assistant as another source. If Home Assistant is added later, it is another way to fill these measurements, not a new selector on Self-production or the shaper.

## Open point

**Does the shaper override stay?** The default is the grid meter. The first version also has an override, because a distinct live-power topic and a standalone HTTP post both exist, and solar-production mode has no grid reading for the shaper to use. The question is whether sites that measure a different point from the service entrance are enough to keep that override.

## Appendix: other silent replacements

| Scenario | What wins | What loses | What the screen says |
|---|---|---|---|
| Shelly is on and a voltage MQTT topic is also set. An HTTP post then sends a voltage. | Shelly, until that post | The voltage MQTT topic. The post then writes the controller. | MQTT settings shows the Shelly sentence. It does not say the post still writes the controller. |
| Both a solar topic and a grid topic are set. Eco is following one of them, and a message arrives on the other. | The feed Eco is following | The other feed, for Eco | The other value is stored, and Eco ignores it. Shelly does not store grid power at all unless the mode is grid excess. The inactive topic is hidden. |
| An HTTP post has set the voltage, and then an MQTT voltage message or a Shelly packet arrives. | The live MQTT or Shelly voltage | The voltage from that post, for the number Eco and the shaper use | Nothing. The post and the live reading take different paths, and the live reading wins on screen. |
| A charger voltage is configured, and a live MQTT or Shelly voltage is arriving. | The live voltage | The configured charger voltage | The charger page shows the configured value with no note that a live meter is overriding it. The live reading is used first, then that configured voltage, then a default. The controller's own voltage reading is left unused. |
| Shelly sends power and voltage more often than every 5 seconds, including voltage changes under 1 V. MQTT or HTTP sends the same kind of readings. | Shelly's own limits | Packets that arrive too soon, or that change voltage by under 1 V | The Shelly page shows age and the last values, and does not say samples are being discarded. MQTT and HTTP apply every sample. [#1402](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/1402) asks for the same 5 second limit on every feed; [#1411](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1411). |
| The broker is already connected in grid excess. Eco is switched to solar production with no restart, and no current subscription is the solar topic. Switching back does the same to the grid topic when no current subscription is that topic. | The subscription taken when the broker connected | The new mode's topic, so Eco gets no samples until the next reconnect or reboot | Nothing. When the shaper topic equals the solar topic and is subscribed on its own, those messages still reach Eco after the switch. They also do when the solar and grid topics are the same text, because the old subscription is that topic. Otherwise the old grid subscription feeds the shaper only when the shaper topic matches the grid topic, and with a blank or different shaper topic it only updates the stored grid value, which Eco ignores in solar mode. Turning Shelly on does reconnect. |
