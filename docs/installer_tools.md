# Installer Tools

Settings → System → **Installer Tools** (directly below Developer Tools) holds the commissioning controls an installer sets once and an owner should not casually change. The page is locked behind an installer password.

## Why a password

NEC 625.42 (2023) lets an EVSE with adjustable current be sized to its *set* current rather than its nameplate, but only if access to the adjusting means is restricted — a tool-only cover, a lock, or password-protected commissioning software available only to qualified personnel. The installer password is the third option. Confirm the wording against the NEC edition adopted where the charger is installed.

## What is on the page

| Section | Notes |
|---|---|
| Hardware maximum current | Sends `$SC xx M` through OpenEVSE_Lib (`setCurrentCapacityFactoryLimit`). One-time only. |
| Safety checks | Moved here from Settings → Safety. The GFCI self-test toggle carries the note "Disable if the circuit is protected by a GFCI." |
| Installer password | Change the password that unlocks the page. |

### Hardware maximum current

The controller accepts `$SC xx M` only while its EEPROM byte (`EOFS_MAX_HW_CURRENT_CAPACITY`) has never been written; any later attempt returns `$NK`, and nothing in RAPI can erase it. The page says so, and tells the installer to set the lower of the station maximum and the circuit maximum. The circuit maximum is the branch-circuit breaker rating × 80%, because an EV charging load is a continuous load (NEC 210.19(A)(1), 210.20(A), 625.42).

The charger can only report that the command was *sent*. The page waits two seconds, re-reads `/config`, and compares `max_current_hard` with the request, so a refused write (already set once) is reported as unchanged rather than as success. The range accepted is 6–80 A, the controller's own limits.

### GFCI self-test advisory removed

The GFCI self-test is legitimately off wherever the circuit is itself GFCI protected, so `safety.gfci_check` is no longer raised as an advisory (firmware rule, label and GUI copy removed). The `gfci_check` setting and its toggle are unchanged. The other five safety-check advisories now link to Installer Tools instead of Safety.

## Password handling

* Default: `installer` (`INSTALLER_PASSWORD_DEFAULT`), used by a fresh or factory-reset unit.
* Stored on the charger as the secret option `installer_password` (short name `ipw`), alongside `www_password`, in plain text.
* **Never sent to the browser**, not even masked: `config_strip_internal()` removes it from every `/config` read and write, every path that goes through `config_deserialize()` (HTTP, MQTT, the serial config command). The GUI therefore cannot verify it locally and asks the charger.
* 4–32 printable ASCII characters.
* The GUI keeps the verified password in memory only; leaving the page locks it again and a reload forgets it.

### Endpoints

All are `POST`, JSON body, require the normal login and the `X-Requested-With: OpenEVSE` header (as the crash-report identity endpoints do).

| Endpoint | Body | Replies |
|---|---|---|
| `/installer/verify` | `{"password"}` | 200 `ok`, 403 `wrong password`, 429 `locked` |
| `/installer/password` | `{"current","new"}` | 200 `changed`, 400 `invalid password`, 403, 429 |
| `/installer/maxcurrent` | `{"password","amps"}` | 200 `sent` (+`requested`, `previous`), 400 `amps out of range`, 403, 429 |

Five wrong passwords lock all three endpoints for 30 seconds (`429` with `retry_after` seconds). The counter is shared and held in RAM, so a reboot clears it.

## Limits to be aware of

This is a commissioning gate, not strong authentication.

* The password is stored in plain text, like `www_password`.
* `POST /config` still accepts `max_current_hard` from any logged-in client, as do the legacy UI, MQTT and RAPI. The controller's write-once rule is what actually protects the hardware limit; the password protects the GUI path. Closing the `/config` path would break those other clients, so it was left alone.
* The charger cannot tell "never set" from "set to 80 A" (80 is the controller default), so the page cannot disable itself once the limit is locked; it reports the outcome after a write attempt.
