#ifndef _OPENEVSE_SELF_SIGNED_CERT_H
#define _OPENEVSE_SELF_SIGNED_CERT_H

#include <Arduino.h>

bool generateSelfSignedCertificate(const String &commonName, const String &ipAddress, String &certPem, String &keyPem);

#endif // _OPENEVSE_SELF_SIGNED_CERT_H
