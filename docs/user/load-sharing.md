# Load sharing

Load sharing lets two to eight OpenEVSE stations share one upstream circuit
limit over the local network. One station is the controller; the other
stations are members.

It is a **Labs preview**: the page is hidden until *Enable Labs features* is
turned on under [Settings → Developer Tools](settings.md) → OpenEVSE Labs, and
every station in the group must run the same firmware release.

## Configure the controller

Open **Settings → Load sharing**, enable the feature, and select
**Controller**. Set:

- the group identifier and total site current;
- the safety factor;
- the allocation-heartbeat timeout;
- the offline-member failsafe mode and reserve currents;
- this station's priority (lower values charge first under scarcity); and
- the equal-priority rotation interval (`0` disables rotation).

Add each member by hostname or IP address, or use discovery. The peer table
shows connectivity, EVSE state, current allocation, and allocation reason.

## Group key

If any station in the group has a web password, every station needs the same
**Group key**, or the stations cannot connect to each other.

1. On the controller, open **Settings → Load sharing** and choose **Generate
   key**.
2. Copy the key that appears. It is shown only once.
3. Paste it into **Group key** on each of the other stations, including ones
   that are already members. This is the one load sharing setting you can edit
   on a member.

You can type your own key instead, using 16 to 64 characters. Do this before
adding a password-protected station to the group, because the controller cannot
reach that station until it has the key.

A peer showing **Group key mismatch** was reached, but it refused the connection.
Check that its key matches and that it runs firmware with this feature.

The key lets stations coordinate charging. It cannot change a station's web
password or other settings. Traffic between stations is authenticated but not
encrypted, so keep them on a network you trust.

To change the key, set the new value on every station, then restart them so
existing connections pick it up.

## Members

The controller pushes group configuration and allocations to members. Load
sharing settings are read-only on a member. To change group settings, use the
controller shown on the member's Load sharing page.

If allocation heartbeats stop, the member applies the configured safe current
or disables charging. A manual override cannot raise charging above this
safety limit.

## Allocation behavior

Charging vehicles share the available current. Connected vehicles that are
not drawing current receive a minimum pilot offer so they can start; that
offer does not consume the physical-current budget. Under scarcity, priority
selects which vehicles receive their minimum and equal-priority peers rotate
at the configured interval.

The dashboard's shared cap is the effective Current Shaper maximum-current
claim. Solar divert, schedules, and manual controls may reduce current further,
but cannot raise it above the load-sharing cap.
