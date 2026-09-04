# Load sharing

Load sharing lets two to eight OpenEVSE stations share one upstream circuit
limit over the local network. One station is the controller; the other
stations are members.

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
