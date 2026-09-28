#ifndef CRASH_HOST_H
#define CRASH_HOST_H

// The single host a core dump may ever be sent to (spec §4).
//
// Compiled in, never a config value. A core dump is a RAM image: the WiFi PSK,
// the MQTT password, OCPP and Tesla tokens and any private key material are all
// in it. If the destination were an ordinary config key, one XSS or one
// authenticated but hostile write would redirect every charger's memory image
// to an attacker -- which, given this project's disclosure history, is a
// requirement rather than a nicety.
//
// The -D override exists for bench work against a test broker. It is a build
// flag, not a runtime value, so a released image still has exactly one
// destination.
#ifndef CRASH_BROKER_HOST
#define CRASH_BROKER_HOST "crash.openevse.com"
#endif

#endif // CRASH_HOST_H
