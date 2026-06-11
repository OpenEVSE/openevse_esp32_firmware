# MQTT Disconnect Fix — Branch `MQTT_Disconnect`

## Problem

When the ESP32 MQTT client attempts to connect to a broker, it sets an internal
`_connecting = true` flag and waits for an `onError` or `onClose` callback to
clear it. If the TCP stack hangs silently — no error, no close event — the flag
stays `true` permanently. The reconnect guard `if (!_connecting)` then prevents
any further connection attempts, and MQTT stops retrying entirely until the
device is rebooted.

This is a known ESP32 failure mode that occurs during:
- Network transitions (WiFi roam, AP channel change)
- Broker restarts or TCP RST packets dropped in transit
- DHCP lease renewals that briefly interrupt the TCP path

A second, minor issue: after a clean disconnect, the task waited up to one full
`MQTT_LOOP_INTERVAL` (50 ms) before scheduling a reconnect, because the
`MicroTask` loop was not explicitly woken.

## Root Cause

`src/mqtt.cpp` — `attemptConnection()` sets `_connecting = true` but records no
timestamp. `loop()` only clears `_connecting` via the `onError`/`onClose`
callbacks. If neither fires, `_connecting` is never reset.

## Fix

**Files changed:** `src/mqtt.cpp`, `src/mqtt.h`  
**Commit:** `e4e54b3`

### 1. `_connectStartTime` watchdog (`src/mqtt.h`, `src/mqtt.cpp`)

A new member `_connectStartTime` (type `unsigned long`) records `millis()` at
the moment `attemptConnection()` sets `_connecting = true`.

In `loop()`, before the connection-state manager runs, a watchdog block checks:

```cpp
if (_connecting && (millis() - _connectStartTime) > (MQTT_CONNECT_TIMEOUT * 2)) {
    DBUGLN("MQTT connection attempt timed out, will retry");
    _connecting = false;
    _nextMqttReconnectAttempt = millis() + MQTT_CONNECT_TIMEOUT;
}
```

`MQTT_CONNECT_TIMEOUT` is 5 000 ms, so the watchdog fires after **10 seconds**
of silence — long enough to absorb normal TCP handshake latency, short enough to
recover quickly from a hung stack. After clearing the flag, the normal retry
path in `loop()` takes over and schedules the next attempt.

### 2. Immediate task wake on disconnect (`src/mqtt.cpp`)

`onMqttDisconnect()` now calls `MicroTask.wakeTask(this)` instead of leaving a
comment that the main loop will handle it:

```cpp
// Before
_connecting = false;
// _nextMqttReconnectAttempt is handled by the main loop to retry.

// After
_connecting = false;
MicroTask.wakeTask(this); // Ensure loop runs promptly to schedule reconnect
```

This ensures the reconnect scheduling runs on the very next scheduler tick
rather than waiting for the natural loop interval.

## What Was Not Changed

These files are **not** touched in this branch:

| File | Reason |
|---|---|
| `src/net_manager.cpp` | WiFi reconnect storm fix — separate concern, separate branch |
| `src/time_man.cpp/.h` | NTP reliability — was unstable when previously merged, needs more work |
| `src/evse_monitor.cpp/.h` | Heartbeat supervision changes — separate concern |
| `src/app_config.cpp/.h` | Heartbeat config persistence — separate concern |
| GUI / web_static | NTP status panel — not related to MQTT |

## Testing

Verify the fix by simulating a TCP hang during MQTT connection:

1. Configure MQTT to point at a host that accepts the TCP connection but never
   sends a CONNACK (e.g., `nc -l 1883` on a laptop).
2. Without the fix: `_connecting` stays `true` and MQTT never retries.
3. With the fix: after ~10 seconds, the debug log prints
   `MQTT connection attempt timed out, will retry` and the client retries on the
   next `MQTT_CONNECT_TIMEOUT` interval.

## Related Issues

Fixes the stuck-reconnect scenario reported in issues #1004, #628.  
Part of the broader `Net_Fixes` branch work; extracted here as a minimal,
reviewable, standalone change.
