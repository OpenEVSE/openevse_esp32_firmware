#pragma once

#include <Arduino.h>

// Return the current session HMAC key.  READ-ONLY: does not generate or
// persist anything.  Returns an empty String when no secret has been
// generated yet (e.g. before web_auth_ensure_secret() has run).
String web_auth_get_secret();

// Ensure a secret exists: if server_secret is empty, generate 32 random bytes
// and persist them via config_user_commit().  Call once at boot from
// config_load_settings() — not from request handlers.
void web_auth_ensure_secret();

// Generate a new random 32-byte (64 hex char) secret and persist it.
// Call whenever web credentials change so that all outstanding sessions are
// invalidated.
void web_auth_rotate_secret();
