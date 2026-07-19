#pragma once

#include <Arduino.h>

// Return the current session HMAC key, generating and persisting it if the
// config value is empty (first boot or after a factory reset).
String web_auth_get_secret();

// Generate a new random 32-byte (64 hex char) secret and persist it.
// Called automatically by web_auth_get_secret() on first use, and explicitly
// whenever web credentials change so that all outstanding sessions are
// invalidated.
void web_auth_rotate_secret();
