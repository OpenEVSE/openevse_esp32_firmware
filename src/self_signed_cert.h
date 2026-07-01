#ifndef _OPENEVSE_SELF_SIGNED_CERT_H
#define _OPENEVSE_SELF_SIGNED_CERT_H

#include <Arduino.h>

// Generates a self-signed ECDSA P-256 certificate (CN=commonName, with a
// SubjectAltName covering both commonName as a DNS name and ipAddress as an
// IP address — modern browsers no longer fall back to CN for hostname
// matching, so a cert without a SAN entry that matches how the browser is
// actually addressing the device gets rejected outright, often surfaced as
// the harsher NET::ERR_CERT_INVALID rather than the normal click-through
// self-signed warning). ipAddress may be empty to skip the IP SAN entry.
// Returns both PEM blocks. Validity is a fixed, generous 2020-2046 window
// rather than "now + N years" — the device may not have NTP-synced time yet
// when this first runs (e.g. on first boot, before WiFi/NTP is even up).
// Returns false on any mbedTLS failure, leaving certPem/keyPem untouched.
bool generateSelfSignedCertificate(const String &commonName, const String &ipAddress, String &certPem, String &keyPem);

#endif // _OPENEVSE_SELF_SIGNED_CERT_H
