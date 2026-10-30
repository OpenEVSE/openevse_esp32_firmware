# Installer tools

**Settings → System → Installer Tools** (`/settings/installer`) holds the
commissioning controls an installer sets once and an owner should not casually
change. It sits directly below Developer Tools and is locked behind an
**installer password**. The default is `installer`; change it on the page
before handing the charger over.

Why a password: under NEC 625.42 an adjustable EVSE may be sized to its *set*
current only if access to the adjusting means is restricted — a tool-only
cover, a lock, or password-protected commissioning software for qualified
personnel. Check the wording against the NEC edition adopted where the charger
is installed.

## Unlocking

Enter the installer password and tap **Unlock**. The charger checks it, not
the browser. After five wrong attempts it refuses further tries for 30 seconds.
The page locks again when you leave it, and a reload forgets the password.

## Hardware maximum current

Sets the charger's hardware maximum — the ceiling no other setting can exceed.
It sends `$SC xx M` to the controller (see [rapi.md](../rapi.md)).

> **One-time setting — use caution.** This command is one-time only. Once
> written, the hardware maximum current cannot be changed or erased. Set it to
> the station maximum or the circuit maximum, whichever is lower.

The circuit maximum is the branch-circuit breaker rating reduced to 80%,
because an EV charging load is a continuous load (NEC 210.19(A)(1), 210.20(A),
625.42). Accepted values run from 6 A up to the current hardware maximum (at
most 80 A): the limit can only be lowered. You are asked to confirm before
anything is sent. The page then reads the value back from the charger and
tells you whether it took; if the controller has already been set once it
keeps the old value, and the page says the change was not accepted.

## Safety checks

The diode, GFCI self-test, ground, stuck-relay, vent and temperature switches
live here (they used to be on the Safety page). Leave them on unless you have a
reason. The **GFCI self-test** has the note *Disable if the circuit is
protected by a GFCI* — that is the legitimate case for turning it off, so it
is not raised as a [notification](notifications.md) when off. See
[Safety](safety.md) for what each check does.

## Installer password

Enter the current password, then the new one twice (4–32 printable
characters). The new password applies immediately and is stored on the charger, so it survives reboots.

## For developers

Endpoints, storage and limits: [installer_tools.md](../installer_tools.md).
