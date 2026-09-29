# Notifications

Notifications are the things you should know about that are **not** faults: a
safety check switched off, a fault that has already cleared, a temperature
reading, relay wear. The charger is still working — but the condition is worth
a look, and it would be lost if it only ever landed in
[History](history.md).

They sit between the two things the charger already does. A fault takes over
the display and stops the session. A history entry waits for you to go looking.
A notification does neither: it raises a bell in the header, and on units with
a colour display it draws an amber border around the screen.

## Where they show up

- **A bell in the web UI header**, with a count. It appears only when there is
  something to show.
- **A strip on the dashboard** for anything critical.
- **A marker beside the control that caused it** — a notification about ground
  check being off appears next to the ground-check switch on
  [Settings → Safety](safety.md), so the fix is one tap away.
- **On a colour display**, an amber border around the whole screen and one line
  naming the condition.

Relay-wear notifications link through to
[Monitoring → Health](monitoring.md), where the figures themselves live.

## What gets raised

| | |
|---|---|
| **Safety** | Any of the six controller checks — ground, GFCI self-test, stuck relay, diode, vent, temperature — being switched **off**. |
| **Faults** | GFCI trips, no-ground trips and stuck-relay faults that have happened and cleared. |
| **Thermal** | The charger throttling on temperature, running close to its shutdown threshold, or reporting a rising relay thermal index. |
| **Relay wear** | Relay life remaining (a notice at 20%, a warning at 5%), contact transit drift, cold opens, and stuck-relay recoveries. |

Relay-wear and relay-thermal notifications need a controller that reports relay
health. On a controller that does not, they are absent entirely — which means
"this charger cannot measure it", not "all is well".

## Acknowledging

Everything can be acknowledged, and what that means depends on the kind.

**A switched-off safety check, or a thermal condition,** is acknowledged by
**muting** it. The condition has not gone away, so it stays in the list marked
as muted, and **the marker beside its switch stays too**. Only the bell count
and the display border stop reacting. Muting an alarm should never make the
charger's configuration a secret.

**A fault counter, relay life, or a cold-open count** is **dismissed** instead.
It stays dismissed until the underlying number moves — so a later GFCI trip
raises it again by itself, with no action from you.

Acknowledgements survive a restart. They are deliberately cleared again by
anything that changes the meaning of the notification: changing the setting it
was about, the counter moving, or updating the firmware. An acknowledgement you
set is for the condition you saw, not forever.

## Why it works this way

The failure mode that matters for a notification system is crying wolf. A
charger that nags about something you have already looked at and decided about
teaches you to ignore it, and then it is worth nothing on the day it matters.
Every rule above follows from that: nothing is raised twice for the same
reason, anything can be silenced, and nothing that is silenced is hidden.
