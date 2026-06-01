# Home Assistant OAuth Connection — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a global Home Assistant connection to the OpenEVSE firmware that authenticates via HA's OAuth2 browser-redirect flow (ESP32 is its own client_id/redirect_uri), maintains tokens, and surfaces a Connect/Disconnect control on the gui-nightshift settings page.

**Architecture:** Pure, dependency-light OAuth logic (`src/ha_oauth.{h,cpp}`, std::string + ArduinoJson) is unit-tested natively with doctest. Arduino/Mongoose glue (`src/home_assistant.{h,cpp}`) owns token lifecycle + refresh as a MicroTask, calls the pure logic, and does HTTP via `MongooseHttpClient`. Web handlers (`src/web_server_home_assistant.cpp`) drive the browser flow. Tokens persist via the existing `ConfigOptSecret` config system. The UI lives in the separate `gui-nightshift` repo (Svelte + vitest).

**Tech Stack:** C++ (Arduino-ESP32, PlatformIO), ArduinoMongoose (`MongooseHttpClient`/`MongooseHttpServer`), ArduinoJson 6.20.1, doctest (native tests), Svelte + Vite + vitest (gui-nightshift).

**Spec:** `docs/superpowers/specs/2026-06-01-home-assistant-oauth-design.md`

**Branch:** `feature/home-assistant-oauth` (already created; spec already committed there).

**Design refinement vs spec:** HA's `/auth/token` endpoint takes `grant_type`, `code`, `client_id` (and `grant_type`, `refresh_token`, `client_id` for refresh) — **not** `redirect_uri`. So the pending-auth stash holds `{state, client_id}`; `redirect_uri` is only used to build the authorize URL. `client_id` must be identical at authorize and token exchange (both derived from the same request Host).

---

## File Structure

**Firmware (this repo):**
- Create `src/ha_oauth.h` — pure OAuth helper declarations (std::string).
- Create `src/ha_oauth.cpp` — pure OAuth helper implementations.
- Create `test/test_ha_oauth/test_ha_oauth.cpp` — doctest unit tests for the pure helpers.
- Create `src/home_assistant.h` / `src/home_assistant.cpp` — `HomeAssistantClient` MicroTask (glue: config, HTTP, refresh).
- Create `src/web_server_home_assistant.cpp` — HTTP handlers for the OAuth flow + status/disconnect.
- Create `test/homeassistant.http` — REST integration script (manual).
- Modify `platformio.ini` — add `[env:native]`.
- Modify `src/app_config.h` / `src/app_config.cpp` — config fields, service flag, dispatch.
- Modify `src/web_server.cpp` — register routes; declare handlers.
- Modify `src/main.cpp` — construct/begin/pump the task.

**UI (`gui-nightshift` repo):**
- Create `src/lib/config/homeassistant.js` — connection-state helper + connect/disconnect actions.
- Create `src/lib/config/__tests__/homeassistant.test.js` — vitest unit tests.
- Create `src/routes/settings/HomeAssistant.svelte` — settings page.
- Create `src/routes/settings/__tests__/HomeAssistant.test.js` — vitest component test.
- Modify `src/lib/config/pages.js` — register the settings page.

---

## Task 1: Native test harness + first pure helper (`ha_derive_base_url`)

**Files:**
- Modify: `platformio.ini` (add `[env:native]` at end of file)
- Create: `src/ha_oauth.h`
- Create: `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

- [ ] **Step 1: Add the native test env to `platformio.ini`**

Append this block to the end of `platformio.ini`:

```ini
[env:native]
; Host-side unit tests for the pure Home Assistant OAuth logic (src/ha_oauth.cpp).
; Run with: pio test -e native
platform = native
test_framework = doctest
test_build_src = true
build_src_filter = -<*> +<ha_oauth.cpp>
build_flags = -std=gnu++17
lib_deps = bblanchon/ArduinoJson@6.20.1
```

- [ ] **Step 2: Create the header `src/ha_oauth.h`**

```cpp
#ifndef HA_OAUTH_H
#define HA_OAUTH_H

#include <string>
#include <cstdint>

// Parsed token response from HA's /auth/token endpoint.
struct HaTokens {
  std::string access_token;
  std::string refresh_token;
  long expires_in = 0; // seconds
};

// Percent-encode a string for use in a URL query value.
std::string ha_url_encode(const std::string &in);

// Build "<scheme>://<host>" with no trailing slash.
// host is the raw HTTP Host header value (may include :port).
std::string ha_derive_base_url(const std::string &scheme, const std::string &host);

// base_url + "/ha_callback"
std::string ha_build_redirect_uri(const std::string &base_url);

// HA authorize URL:
//   <ha_url>/auth/authorize?response_type=code&client_id=..&redirect_uri=..&state=..
std::string ha_build_authorize_url(const std::string &ha_url,
                                   const std::string &client_id,
                                   const std::string &redirect_uri,
                                   const std::string &state);

// x-www-form-urlencoded body for grant_type=authorization_code.
std::string ha_build_token_exchange_body(const std::string &client_id,
                                         const std::string &code);

// x-www-form-urlencoded body for grant_type=refresh_token.
std::string ha_build_refresh_body(const std::string &client_id,
                                  const std::string &refresh_token);

// Parse the /auth/token JSON response. Returns true on success
// (access_token and expires_in present); refresh_token may be empty on refresh.
bool ha_parse_token_response(const std::string &json, HaTokens &out);

// Absolute expiry = now_unix + expires_in (0 if expires_in <= 0).
uint64_t ha_compute_expiry(uint64_t now_unix, long expires_in);

// True if the token should be refreshed: now_unix >= expiry_unix - margin_sec.
bool ha_refresh_due(uint64_t expiry_unix, uint64_t now_unix, uint64_t margin_sec);

#endif // HA_OAUTH_H
```

- [ ] **Step 3: Create `src/ha_oauth.cpp` with only `ha_url_encode` + `ha_derive_base_url` implemented**

```cpp
#include "ha_oauth.h"
#include <cctype>

std::string ha_url_encode(const std::string &in) {
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size() * 3);
  for (unsigned char c : in) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

std::string ha_derive_base_url(const std::string &scheme, const std::string &host) {
  std::string s = scheme.empty() ? "http" : scheme;
  std::string h = host;
  // strip any trailing slash on host (defensive)
  while (!h.empty() && h.back() == '/') h.pop_back();
  return s + "://" + h;
}
```

- [ ] **Step 4: Write the failing test `test/test_ha_oauth/test_ha_oauth.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ha_oauth.h"

TEST_CASE("ha_url_encode percent-encodes reserved characters") {
  CHECK(ha_url_encode("abc-123_.~") == "abc-123_.~");
  CHECK(ha_url_encode("http://h:8123/") == "http%3A%2F%2Fh%3A8123%2F");
  CHECK(ha_url_encode("a b") == "a%20b");
}

TEST_CASE("ha_derive_base_url joins scheme and host") {
  CHECK(ha_derive_base_url("http", "openevse.local") == "http://openevse.local");
  CHECK(ha_derive_base_url("https", "10.0.0.5:443") == "https://10.0.0.5:443");
  CHECK(ha_derive_base_url("", "h") == "http://h");          // default scheme
  CHECK(ha_derive_base_url("http", "h/") == "http://h");     // trailing slash stripped
}
```

- [ ] **Step 5: Run the test to verify it builds & passes**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS — `test_ha_oauth.cpp` compiles, both `TEST_CASE`s green. (doctest is fetched automatically by PlatformIO's doctest runner.)

If it fails to find `doctest.h`: confirm `test_framework = doctest` is set; PlatformIO injects the doctest include path for the test build.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini src/ha_oauth.h src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "test(ha): native doctest harness + ha_url_encode/ha_derive_base_url

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Authorize-URL builders (`ha_build_redirect_uri`, `ha_build_authorize_url`)

**Files:**
- Modify: `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_ha_oauth/test_ha_oauth.cpp`:

```cpp
TEST_CASE("ha_build_redirect_uri appends the callback path") {
  CHECK(ha_build_redirect_uri("http://openevse.local") ==
        "http://openevse.local/ha_callback");
}

TEST_CASE("ha_build_authorize_url builds an encoded query") {
  std::string url = ha_build_authorize_url(
      "http://homeassistant.local:8123",
      "http://openevse.local/",
      "http://openevse.local/ha_callback",
      "abc123");
  CHECK(url ==
        "http://homeassistant.local:8123/auth/authorize"
        "?response_type=code"
        "&client_id=http%3A%2F%2Fopenevse.local%2F"
        "&redirect_uri=http%3A%2F%2Fopenevse.local%2Fha_callback"
        "&state=abc123");
}

TEST_CASE("ha_build_authorize_url strips a trailing slash from ha_url") {
  std::string url = ha_build_authorize_url(
      "http://ha:8123/", "cid", "ruri", "s");
  CHECK(url.rfind("http://ha:8123/auth/authorize", 0) == 0);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_ha_oauth`
Expected: FAIL — link error / undefined reference to `ha_build_redirect_uri` and `ha_build_authorize_url`.

- [ ] **Step 3: Implement in `src/ha_oauth.cpp`**

Append:

```cpp
std::string ha_build_redirect_uri(const std::string &base_url) {
  return base_url + "/ha_callback";
}

std::string ha_build_authorize_url(const std::string &ha_url,
                                   const std::string &client_id,
                                   const std::string &redirect_uri,
                                   const std::string &state) {
  std::string base = ha_url;
  while (!base.empty() && base.back() == '/') base.pop_back();
  std::string url = base + "/auth/authorize";
  url += "?response_type=code";
  url += "&client_id=" + ha_url_encode(client_id);
  url += "&redirect_uri=" + ha_url_encode(redirect_uri);
  url += "&state=" + ha_url_encode(state);
  return url;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS — all cases green.

- [ ] **Step 5: Commit**

```bash
git add src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "feat(ha): authorize-URL + redirect-URI builders

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Token request body builders

**Files:**
- Modify: `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

- [ ] **Step 1: Add failing tests**

Append:

```cpp
TEST_CASE("ha_build_token_exchange_body encodes the form body") {
  CHECK(ha_build_token_exchange_body("http://openevse.local/", "the+code") ==
        "grant_type=authorization_code"
        "&code=the%2Bcode"
        "&client_id=http%3A%2F%2Fopenevse.local%2F");
}

TEST_CASE("ha_build_refresh_body encodes the form body") {
  CHECK(ha_build_refresh_body("cid", "rtok") ==
        "grant_type=refresh_token"
        "&refresh_token=rtok"
        "&client_id=cid");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_ha_oauth`
Expected: FAIL — undefined references.

- [ ] **Step 3: Implement**

Append to `src/ha_oauth.cpp`:

```cpp
std::string ha_build_token_exchange_body(const std::string &client_id,
                                         const std::string &code) {
  std::string b = "grant_type=authorization_code";
  b += "&code=" + ha_url_encode(code);
  b += "&client_id=" + ha_url_encode(client_id);
  return b;
}

std::string ha_build_refresh_body(const std::string &client_id,
                                  const std::string &refresh_token) {
  std::string b = "grant_type=refresh_token";
  b += "&refresh_token=" + ha_url_encode(refresh_token);
  b += "&client_id=" + ha_url_encode(client_id);
  return b;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "feat(ha): token-exchange + refresh form-body builders

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Token-response parser (ArduinoJson)

**Files:**
- Modify: `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

- [ ] **Step 1: Add failing tests**

Append:

```cpp
TEST_CASE("ha_parse_token_response extracts tokens and expiry") {
  HaTokens t;
  bool ok = ha_parse_token_response(
      "{\"access_token\":\"AT\",\"refresh_token\":\"RT\","
      "\"expires_in\":1800,\"token_type\":\"Bearer\"}", t);
  CHECK(ok);
  CHECK(t.access_token == "AT");
  CHECK(t.refresh_token == "RT");
  CHECK(t.expires_in == 1800);
}

TEST_CASE("ha_parse_token_response: refresh response without refresh_token is ok") {
  HaTokens t;
  bool ok = ha_parse_token_response(
      "{\"access_token\":\"AT2\",\"expires_in\":1800}", t);
  CHECK(ok);
  CHECK(t.access_token == "AT2");
  CHECK(t.refresh_token.empty());
}

TEST_CASE("ha_parse_token_response fails on error / malformed bodies") {
  HaTokens t;
  CHECK_FALSE(ha_parse_token_response("{\"error\":\"invalid_grant\"}", t));
  CHECK_FALSE(ha_parse_token_response("not json", t));
  CHECK_FALSE(ha_parse_token_response("{\"expires_in\":1800}", t)); // no access_token
}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_ha_oauth`
Expected: FAIL — undefined reference to `ha_parse_token_response`.

- [ ] **Step 3: Implement**

Add the include at the top of `src/ha_oauth.cpp` (below the existing includes):

```cpp
#include <ArduinoJson.h>
```

Append:

```cpp
bool ha_parse_token_response(const std::string &json, HaTokens &out) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    return false;
  }
  if (!doc["access_token"].is<const char *>()) {
    return false;
  }
  out.access_token = doc["access_token"].as<const char *>();
  if (doc["refresh_token"].is<const char *>()) {
    out.refresh_token = doc["refresh_token"].as<const char *>();
  } else {
    out.refresh_token.clear();
  }
  out.expires_in = doc["expires_in"] | 0L;
  return true;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "feat(ha): parse /auth/token JSON response

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Expiry / refresh-due helpers

**Files:**
- Modify: `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

- [ ] **Step 1: Add failing tests**

Append:

```cpp
TEST_CASE("ha_compute_expiry adds expires_in to now") {
  CHECK(ha_compute_expiry(1000, 1800) == 2800);
  CHECK(ha_compute_expiry(1000, 0) == 0);    // unknown expiry
  CHECK(ha_compute_expiry(1000, -5) == 0);
}

TEST_CASE("ha_refresh_due triggers within the margin") {
  // expiry at 2800, margin 300 -> refresh due at >= 2500
  CHECK_FALSE(ha_refresh_due(2800, 2499, 300));
  CHECK(ha_refresh_due(2800, 2500, 300));
  CHECK(ha_refresh_due(2800, 3000, 300));    // already expired
  CHECK(ha_refresh_due(0, 5, 300));          // unknown expiry -> always due
}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_ha_oauth`
Expected: FAIL — undefined references.

- [ ] **Step 3: Implement**

Append to `src/ha_oauth.cpp`:

```cpp
uint64_t ha_compute_expiry(uint64_t now_unix, long expires_in) {
  if (expires_in <= 0) return 0;
  return now_unix + (uint64_t)expires_in;
}

bool ha_refresh_due(uint64_t expiry_unix, uint64_t now_unix, uint64_t margin_sec) {
  if (expiry_unix == 0) return true; // unknown expiry -> refresh
  uint64_t threshold = (expiry_unix > margin_sec) ? (expiry_unix - margin_sec) : 0;
  return now_unix >= threshold;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS — entire `ha_oauth` suite green.

- [ ] **Step 5: Commit**

```bash
git add src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "feat(ha): token expiry + refresh-due calculators

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Config fields + service flag + dispatch

**Files:**
- Modify: `src/app_config.h` (service-flag defines + accessor)
- Modify: `src/app_config.cpp` (variable decls, ConfigOpt registrations, dispatch)

No unit test (config is glue with no native harness); verified by the firmware build in Task 7 and the `.http` script in Task 10.

- [ ] **Step 1: Add the service-flag define and accessor in `src/app_config.h`**

After the line `#define CONFIG_SERVICE_CUR_SHAPER   (1 << 19)` add:

```cpp
#define CONFIG_SERVICE_HOMEASSISTANT (1 << 20)
```

After the existing `config_current_shaper_enabled()` inline accessor (near the other `config_*_enabled()` helpers), add:

```cpp
inline bool config_home_assistant_enabled() {
  return CONFIG_SERVICE_HOMEASSISTANT == (flags & CONFIG_SERVICE_HOMEASSISTANT);
}
```

Also add `extern` declarations alongside the other `extern String ...;` config externs in `app_config.h` (find the block that declares `extern String tesla_access_token;` etc.; if Tesla externs are not present there, add these next to the other `extern String` lines):

```cpp
// Home Assistant settings
extern String ha_url;
extern String ha_access_token;
extern String ha_refresh_token;
extern uint64_t ha_token_expires;
extern String ha_client_id;   // the device URL advertised at authorize; replayed on refresh
```

- [ ] **Step 2: Declare the storage variables in `src/app_config.cpp`**

After the Tesla settings variable block (the lines `String tesla_access_token; ...`), add:

```cpp
// Home Assistant settings
String ha_url;
String ha_access_token;
String ha_refresh_token;
uint64_t ha_token_expires;
String ha_client_id;
```

- [ ] **Step 3: Register the ConfigOpt entries in `src/app_config.cpp`**

In the config options list (the `ConfigOpt *opts[] = { ... }` array — find it by the existing `new ConfigOptSecret(tesla_access_token, ...)` lines), add after the Tesla block:

```cpp
// Home Assistant settings
  new ConfigOptDefinition<String>(ha_url, "", "ha_url", "hau"),
  new ConfigOptSecret(ha_access_token, "", "ha_access_token", "haa"),
  new ConfigOptSecret(ha_refresh_token, "", "ha_refresh_token", "har"),
  new ConfigOptDefinition<uint64_t>(ha_token_expires, 0, "ha_token_expires", "hax"),
  new ConfigOptDefinition<String>(ha_client_id, "", "ha_client_id", "hac"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_HOMEASSISTANT, CONFIG_SERVICE_HOMEASSISTANT, "home_assistant_enabled", "hae"),
```

- [ ] **Step 4: Add the change-dispatch branch in `src/app_config.cpp`**

Add `#include "home_assistant.h"` near the top with the other includes (e.g. after `#include "tesla_client.h"`).

In `config_changed(String name)` (the function with the `} else if(name.startsWith("tesla_")) {` branch), add before the closing of that if/else chain:

```cpp
  } else if(name.startsWith("ha_") || name == "home_assistant_enabled") {
    homeAssistant.notifyConfigChanged();
```

- [ ] **Step 5: Verify it compiles (build deferred to Task 7)**

`home_assistant.h` / `homeAssistant` do not exist yet — this task does not build on its own. It is completed together with Task 7; do not commit until Task 7's build passes. (Mark this task's checkboxes done, then proceed straight to Task 7.)

---

## Task 7: `HomeAssistantClient` skeleton + config wiring + build

**Files:**
- Create: `src/home_assistant.h`
- Create: `src/home_assistant.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Create `src/home_assistant.h`**

```cpp
#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>
#include <MongooseHttpClient.h>

#include "ha_oauth.h"

class HomeAssistantClient : public MicroTasks::Task {
  public:
    HomeAssistantClient();

    void begin();

    // True if enabled and we hold a usable (or refreshable) token.
    bool isConnected();

    // Fill {enabled, connected, ha_url, expires} for the /ha/status endpoint.
    void getStatus(JsonDocument &doc);

    // Build the HA authorize URL for a browser redirect. `host` is the request
    // Host header, `secure` whether the request arrived over TLS. Generates and
    // stashes a fresh CSRF state + the derived client_id. Returns "" if ha_url is
    // not configured.
    String beginAuthorize(const String &host, bool secure);

    // Handle the /ha_callback: verify state, exchange the code for tokens.
    // Returns true if the exchange request was dispatched (async result completes
    // later). Sets `error` on synchronous validation failure.
    bool handleCallback(const String &code, const String &state, String &error);

    // Clear tokens + pending auth; cancels refresh.
    void disconnect();

    // Re-read credentials from config (called from config_changed dispatch).
    void notifyConfigChanged();

    // Reusable authed GET seam for future consumers. Attaches Bearer header.
    // (Present in v1; no callers yet.)
    void get(const String &path, MongooseHttpResponseHandler onResponse);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  private:
    MongooseHttpClient _client;

    // pending auth (single slot)
    String _pendingState;
    String _pendingClientId;
    uint32_t _pendingStateTime; // millis() when issued; 0 = none

    bool _refreshInFlight;
    unsigned long _lastRefreshAttempt;

    void exchangeCode(const String &code);
    void refreshTokens();
    void storeTokens(const HaTokens &t);
};

extern HomeAssistantClient homeAssistant;

#endif // HOME_ASSISTANT_H
```

- [ ] **Step 2: Create `src/home_assistant.cpp` (skeleton: lifecycle, status, config, no flow yet)**

```cpp
#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_HOME_ASSISTANT)
#undef ENABLE_DEBUG
#endif

#include <Arduino.h>
#include <time.h>

#include "home_assistant.h"
#include "app_config.h"
#include "debug.h"
#include "event.h"

#define HA_PENDING_STATE_TTL_MS   (5 * 60 * 1000UL) // 5 min to complete the browser flow
#define HA_REFRESH_MARGIN_SEC     300               // refresh 5 min before expiry
#define HA_REFRESH_RETRY_MS       (60 * 1000UL)     // backoff after a failed refresh
#define HA_LOOP_INTERVAL_MS       (30 * 1000UL)

HomeAssistantClient homeAssistant;

static uint64_t ha_now_unix() {
  time_t now = time(nullptr);
  return (now > 0) ? (uint64_t)now : 0;
}

HomeAssistantClient::HomeAssistantClient() :
  MicroTasks::Task(),
  _pendingStateTime(0),
  _refreshInFlight(false),
  _lastRefreshAttempt(0)
{
}

void HomeAssistantClient::begin() {
  MicroTask.startTask(this);
}

void HomeAssistantClient::setup() {
}

bool HomeAssistantClient::isConnected() {
  if (!config_home_assistant_enabled()) return false;
  if (ha_refresh_token.length() == 0) return false;
  // Connected if we have an access token that is not past expiry, OR we can refresh.
  return ha_access_token.length() > 0 || ha_refresh_token.length() > 0;
}

void HomeAssistantClient::getStatus(JsonDocument &doc) {
  doc["enabled"] = config_home_assistant_enabled();
  doc["connected"] = isConnected();
  doc["ha_url"] = ha_url;
  doc["expires"] = ha_token_expires;
}

void HomeAssistantClient::notifyConfigChanged() {
  // Credentials/url live in the global config vars; nothing to cache.
  // Wake so loop() re-evaluates refresh scheduling immediately.
  MicroTask.wakeTask(this);
}

void HomeAssistantClient::disconnect() {
  ha_access_token = "";
  ha_refresh_token = "";
  ha_token_expires = 0;
  _pendingStateTime = 0;
  _pendingState = "";
  _pendingClientId = "";
  config_commit(); // persist cleared secrets
  StaticJsonDocument<128> ev;
  ev["home_assistant"] = "disconnected";
  event_send(ev);
}

unsigned long HomeAssistantClient::loop(MicroTasks::WakeReason reason) {
  // Expire a stale pending auth.
  if (_pendingStateTime != 0 &&
      (millis() - _pendingStateTime) > HA_PENDING_STATE_TTL_MS) {
    _pendingStateTime = 0;
    _pendingState = "";
    _pendingClientId = "";
  }

  // Refresh scheduling (implemented in Task 9).
  return HA_LOOP_INTERVAL_MS;
}

// --- flow methods are implemented in Tasks 8 & 9 ---
String HomeAssistantClient::beginAuthorize(const String &host, bool secure) {
  (void)host; (void)secure;
  return "";
}

bool HomeAssistantClient::handleCallback(const String &code, const String &state, String &error) {
  (void)code; (void)state;
  error = "not implemented";
  return false;
}

void HomeAssistantClient::exchangeCode(const String &code) { (void)code; }
void HomeAssistantClient::refreshTokens() {}
void HomeAssistantClient::storeTokens(const HaTokens &t) { (void)t; }

void HomeAssistantClient::get(const String &path, MongooseHttpResponseHandler onResponse) {
  (void)path; (void)onResponse;
}
```

Notes for the implementer:
- `config_commit()` is the existing persist call used elsewhere in the firmware (confirm its exact name in `app_config.h` — search for how other code saves config after mutating a config var, e.g. in `web_server_config.cpp`; use the same call).
- `event_send(JsonDocument&)` is declared in `event.h`.

- [ ] **Step 3: Wire into `src/main.cpp`**

Add the include near the other integration includes (e.g. after `#include "tesla_client.h"`):

```cpp
#include "home_assistant.h"
```

In `setup()`, alongside the other `*.begin()` calls for integration tasks (search for `teslaClient` / `mqtt.begin()` style init), add:

```cpp
  homeAssistant.begin();
```

(The MicroTask scheduler pumps `loop()` automatically once started; no per-loop call needed — confirm by checking that other `MicroTasks::Task`-derived integrations are not manually pumped in the main `loop()`.)

- [ ] **Step 4: Build the firmware (this also validates Task 6)**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS. (`nodemcu-32s` is a fast core-2 ESP32 env; it exercises the new files and config without P4 build time.)

- [ ] **Step 5: Run the native tests to confirm no regression**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 6: Commit (includes Task 6's changes)**

```bash
git add src/app_config.h src/app_config.cpp src/home_assistant.h src/home_assistant.cpp src/main.cpp
git commit -m "feat(ha): config fields + HomeAssistantClient skeleton wired into setup

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Authorize + callback (code→token exchange)

**Files:**
- Modify: `src/home_assistant.cpp`

- [ ] **Step 1: Implement `beginAuthorize`**

Replace the stub `beginAuthorize` with:

```cpp
String HomeAssistantClient::beginAuthorize(const String &host, bool secure) {
  if (ha_url.length() == 0 || host.length() == 0) {
    return "";
  }

  std::string scheme = secure ? "https" : "http";
  std::string base = ha_derive_base_url(scheme, std::string(host.c_str()));
  std::string clientId = base + "/";
  std::string redirectUri = ha_build_redirect_uri(base);

  // 16 hex chars of CSRF state from the HW RNG.
  char stateBuf[17];
  for (int i = 0; i < 16; i++) {
    stateBuf[i] = "0123456789abcdef"[esp_random() & 0xF];
  }
  stateBuf[16] = '\0';

  _pendingState = stateBuf;
  _pendingClientId = clientId.c_str();
  _pendingStateTime = millis();
  if (_pendingStateTime == 0) _pendingStateTime = 1; // 0 means "none"

  // Persist the client_id so token refresh can replay it after a reboot
  // (it is derived from the device Host, which we don't otherwise know later).
  ha_client_id = clientId.c_str();
  config_commit();

  std::string url = ha_build_authorize_url(
      std::string(ha_url.c_str()), clientId, redirectUri, _pendingState.c_str());
  return String(url.c_str());
}
```

Add `#include <esp_random.h>` near the top of `src/home_assistant.cpp` (below `#include <time.h>`).

- [ ] **Step 2: Implement `handleCallback` + `exchangeCode` + `storeTokens`**

Replace the stub `handleCallback`, `exchangeCode`, and `storeTokens` with:

```cpp
bool HomeAssistantClient::handleCallback(const String &code, const String &state, String &error) {
  if (_pendingStateTime == 0) {
    error = "no pending authorization";
    return false;
  }
  if (state.length() == 0 || state != _pendingState) {
    error = "state mismatch";
    return false;
  }
  if (code.length() == 0) {
    error = "missing code";
    return false;
  }
  // Consume the pending state (single use); keep _pendingClientId for the exchange.
  _pendingStateTime = 0;
  _pendingState = "";
  exchangeCode(code);
  return true;
}

void HomeAssistantClient::storeTokens(const HaTokens &t) {
  ha_access_token = t.access_token.c_str();
  if (!t.refresh_token.empty()) {
    ha_refresh_token = t.refresh_token.c_str();
  }
  ha_token_expires = ha_compute_expiry(ha_now_unix(), t.expires_in);
  config_commit();

  StaticJsonDocument<128> ev;
  ev["home_assistant"] = "connected";
  event_send(ev);
}

void HomeAssistantClient::exchangeCode(const String &code) {
  if (ha_url.length() == 0 || _pendingClientId.length() == 0) {
    return;
  }
  std::string body = ha_build_token_exchange_body(
      std::string(_pendingClientId.c_str()), std::string(code.c_str()));

  String uri = ha_url;
  while (uri.endsWith("/")) uri.remove(uri.length() - 1);
  uri += "/auth/token";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_POST);
  req->addHeader("Content-Type", "application/x-www-form-urlencoded");
  req->setContent((const uint8_t *)body.c_str(), body.length());
  req->onResponse([this](MongooseHttpClientResponse *response) {
    if (response->respCode() == 200) {
      HaTokens t;
      std::string json((const char *)response->body(), response->body().length());
      if (ha_parse_token_response(json, t)) {
        storeTokens(t);
        return;
      }
    }
    StaticJsonDocument<128> ev;
    ev["home_assistant"] = "error";
    ev["home_assistant_error"] = "token exchange failed";
    event_send(ev);
  });
  _client.send(req);
}
```

- [ ] **Step 2b: Confirm `esp_random.h` is acceptable on core-2 builds**

`esp_random()` is available on all ESP32 targets. The include `<esp_random.h>` resolves on core-2 (IDF4) and core-3 (IDF5). No gating needed (unlike the P4-only path in `net_manager.cpp`).

- [ ] **Step 3: Build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 4: Native tests still pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS (no pure-logic changes, but confirms nothing broke the shared header).

- [ ] **Step 5: Commit**

```bash
git add src/home_assistant.cpp
git commit -m "feat(ha): authorize-URL generation + callback code->token exchange

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Refresh scheduling + Bearer `get()` seam

**Files:**
- Modify: `src/home_assistant.cpp`

(The persisted `ha_client_id` config field this task relies on was added in Task 6 and is set in Task 8.)

The persisted `ha_client_id` field (added in Task 6, set in Task 8's `beginAuthorize`) supplies the `client_id` for refresh, which must match the one used at authorize and survives reboots.

- [ ] **Step 1: Implement `refreshTokens`**

Replace the stub `refreshTokens` with:

```cpp
void HomeAssistantClient::refreshTokens() {
  if (_refreshInFlight) return;
  if (ha_url.length() == 0 || ha_refresh_token.length() == 0) return;
  if (ha_client_id.length() == 0) return; // never authorized

  _refreshInFlight = true;
  _lastRefreshAttempt = millis();

  std::string clientId = std::string(ha_client_id.c_str());
  std::string body = ha_build_refresh_body(clientId, std::string(ha_refresh_token.c_str()));

  String uri = ha_url;
  while (uri.endsWith("/")) uri.remove(uri.length() - 1);
  uri += "/auth/token";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_POST);
  req->addHeader("Content-Type", "application/x-www-form-urlencoded");
  req->setContent((const uint8_t *)body.c_str(), body.length());
  req->onResponse([this](MongooseHttpClientResponse *response) {
    _refreshInFlight = false;
    if (response->respCode() == 200) {
      HaTokens t;
      std::string json((const char *)response->body(), response->body().length());
      if (ha_parse_token_response(json, t)) {
        storeTokens(t);
        return;
      }
    }
    if (response->respCode() == 400) {
      // invalid_grant: refresh token revoked -> disconnect.
      disconnect();
    }
    // transient errors: leave tokens; loop() retries after backoff.
  });
  _client.send(req);
}
```

- [ ] **Step 1b: Drive refresh from `loop()`**

In `loop()`, replace the comment `// Refresh scheduling (implemented in Task 9).` with:

```cpp
  if (config_home_assistant_enabled() && ha_refresh_token.length() > 0 && !_refreshInFlight) {
    bool due = ha_refresh_due(ha_token_expires, ha_now_unix(), HA_REFRESH_MARGIN_SEC);
    bool backoffElapsed = (millis() - _lastRefreshAttempt) > HA_REFRESH_RETRY_MS;
    if (due && (_lastRefreshAttempt == 0 || backoffElapsed)) {
      refreshTokens();
    }
  }
```

- [ ] **Step 2: Implement the `get()` seam**

Replace the stub `get` with:

```cpp
void HomeAssistantClient::get(const String &path, MongooseHttpResponseHandler onResponse) {
  if (!isConnected() || ha_url.length() == 0) return;

  String uri = ha_url;
  while (uri.endsWith("/")) uri.remove(uri.length() - 1);
  uri += path; // caller passes a leading-slash path, e.g. "/api/states/sensor.x"

  String bearer = "Bearer ";
  bearer += ha_access_token;

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_GET);
  req->addHeader("Authorization", bearer.c_str());
  req->addHeader("Content-Type", "application/json");
  req->onResponse(onResponse);
  _client.send(req);
}
```

- [ ] **Step 3: Build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 4: Native tests pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/home_assistant.cpp
git commit -m "feat(ha): token refresh scheduling + Bearer get() seam

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Web endpoints + route registration + .http script

**Files:**
- Create: `src/web_server_home_assistant.cpp`
- Modify: `src/web_server.cpp` (declare handlers + register routes)
- Create: `test/homeassistant.http`

- [ ] **Step 1: Create `src/web_server_home_assistant.cpp`**

```cpp
#include "emonesp.h"
#include "web_server.h"
#include "app_config.h"
#include "home_assistant.h"
#include "debug.h"

#include <ArduinoJson.h>
#include <MongooseHttpServer.h>

// GET /ha/auth/start  -> 302 to HA authorize URL
void handleHomeAssistantAuthStart(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response, JSON_CONTENT_TYPE)) {
    return;
  }

  String host = request->host().toString();
  bool secure = request->isSecure(); // confirm exact accessor in MongooseHttpServerRequest
  String url = homeAssistant.beginAuthorize(host, secure);

  if (url.length() == 0) {
    response->setCode(400);
    response->print("{\"msg\":\"ha_url not configured\"}");
    request->send(response);
    return;
  }

  // Redirect the browser to Home Assistant's login/authorize page.
  request->redirect(url);
  // requestPreProcess allocated `response`; if redirect() consumes the request,
  // free the unused stream per the pattern used by other redirecting handlers.
  delete response;
}

// GET /ha_callback?code=..&state=..  -> exchange, then redirect to settings
void handleHomeAssistantCallback(MongooseHttpServerRequest *request)
{
  // NOTE: no requestPreProcess/basic-auth here — HA's browser redirect cannot
  // carry HTTP auth. Protected by the unguessable single-use `state`.
  String code = request->getParam("code");
  String state = request->getParam("state");

  String error;
  bool ok = homeAssistant.handleCallback(code, state, error);

  // Redirect back into the UI with a result flag for the toast.
  String dest = "/#/settings/home-assistant?ha=";
  dest += ok ? "connected" : "error";
  request->redirect(dest);
}

// GET /ha/status -> {enabled, connected, ha_url, expires}
void handleHomeAssistantStatus(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response, JSON_CONTENT_TYPE)) {
    return;
  }
  const size_t capacity = JSON_OBJECT_SIZE(4) + 256;
  DynamicJsonDocument doc(capacity);
  homeAssistant.getStatus(doc);
  serializeJson(doc, *response);
  request->send(response);
}

// POST /ha/disconnect -> clear tokens
void handleHomeAssistantDisconnect(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if (false == requestPreProcess(request, response, JSON_CONTENT_TYPE)) {
    return;
  }
  homeAssistant.disconnect();
  response->print("{\"msg\":\"done\"}");
  request->send(response);
}
```

Implementer notes (verify against `MongooseHttpServer.h`, which is already open in this plan's research):
- `request->host()` returns a `MongooseString`; `.toString()` or constructing `String(request->host().c_str())` — match how other handlers convert. (`MongooseString` has `c_str()`.)
- `request->isSecure()` — confirm the exact accessor name for "did this arrive on the TLS server". If absent, derive `secure` by checking which `MongooseHttpServer` instance served it, or default `false` (LAN http is the common case) and revisit. Do NOT block the task on TLS detection; `false` is a safe default for v1.
- `requestPreProcess`, `JSON_CONTENT_TYPE`, and the redirect/`delete response` idiom: copy exactly from an existing handler that redirects (e.g. `handleAPOff` or the update handlers in `web_server.cpp`). Match the established freeing pattern rather than guessing.

- [ ] **Step 2: Declare handlers + register routes in `src/web_server.cpp`**

Near the other `void handleX(MongooseHttpServerRequest *request);` forward declarations (around line 73), add:

```cpp
void handleHomeAssistantAuthStart(MongooseHttpServerRequest *request);
void handleHomeAssistantCallback(MongooseHttpServerRequest *request);
void handleHomeAssistantStatus(MongooseHttpServerRequest *request);
void handleHomeAssistantDisconnect(MongooseHttpServerRequest *request);
```

In `web_server_setup()` near the other `server.on(...)` calls (around line 1203), add:

```cpp
  server.on("/ha/auth/start$", handleHomeAssistantAuthStart);
  server.on("/ha_callback", handleHomeAssistantCallback);
  server.on("/ha/status$", handleHomeAssistantStatus);
  server.on("/ha/disconnect$", handleHomeAssistantDisconnect);
```

(Note: `/ha_callback` has no `$` anchor so it matches with the query string, consistent with how `/schedule`, `/claims` are registered without anchors.)

- [ ] **Step 3: Create `test/homeassistant.http` integration script**

```http
# Home Assistant OAuth endpoints (manual integration test).
# Replace HOST + auth as in the other .http scripts in this dir.
@host = 10.75.1.143
@auth = Basic <base64 user:pass>

### status (expect {enabled, connected, ha_url, expires})
GET http://{{host}}/ha/status
Authorization: {{auth}}

### start auth (expect 302 Location: <ha_url>/auth/authorize?...)
# Use a browser for the real flow; curl -i shows the redirect.
GET http://{{host}}/ha/auth/start
Authorization: {{auth}}

### disconnect (expect {"msg":"done"})
POST http://{{host}}/ha/disconnect
Authorization: {{auth}}
```

- [ ] **Step 4: Build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/web_server_home_assistant.cpp src/web_server.cpp test/homeassistant.http
git commit -m "feat(ha): web endpoints for OAuth start/callback/status/disconnect

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: gui-nightshift — connection helper + unit tests

**Files (in `gui-nightshift/`):**
- Create: `src/lib/config/homeassistant.js`
- Test: `src/lib/config/__tests__/homeassistant.test.js`

Work in the `gui-nightshift` submodule directory. Commit there separately.

- [ ] **Step 1: Write the failing test `src/lib/config/__tests__/homeassistant.test.js`**

```js
// src/lib/config/__tests__/homeassistant.test.js
import { describe, it, expect, vi, beforeEach } from 'vitest'
import { isHaConnected, startHaAuth } from '../homeassistant.js'

describe('isHaConnected', () => {
  it('is true only when status reports connected', () => {
    expect(isHaConnected({ enabled: true, connected: true })).toBe(true)
    expect(isHaConnected({ enabled: true, connected: false })).toBe(false)
    expect(isHaConnected({})).toBe(false)
    expect(isHaConnected(undefined)).toBe(false)
  })
})

describe('startHaAuth', () => {
  beforeEach(() => {
    // jsdom: make location assignable
    delete window.location
    window.location = { href: '' }
  })
  it('navigates the browser to the firmware start endpoint', () => {
    startHaAuth()
    expect(window.location.href).toContain('/ha/auth/start')
  })
})
```

- [ ] **Step 2: Run to verify failure**

Run (in `gui-nightshift/`): `npm test -- homeassistant`
Expected: FAIL — cannot resolve `../homeassistant.js`.

- [ ] **Step 3: Implement `src/lib/config/homeassistant.js`**

```js
// src/lib/config/homeassistant.js
// Home Assistant connection helpers.
import { httpAPI } from '../api/httpAPI.js'

export function isHaConnected(status) {
  return !!(status && status.connected)
}

// Full browser navigation so HA's login page + redirect chain run in the
// real browser (not via fetch). In dev, httpAPI prefixes /api; for a top-level
// navigation we hit the firmware path directly.
export function startHaAuth() {
  const base = import.meta.env.DEV ? '/api' : ''
  window.location.href = base + '/ha/auth/start'
}

export async function fetchHaStatus() {
  const res = await httpAPI('GET', '/ha/status')
  return res && res !== 'error' ? res : null
}

export async function disconnectHa() {
  const res = await httpAPI('POST', '/ha/disconnect')
  return res && (res.msg === 'done')
}
```

- [ ] **Step 4: Run to verify pass**

Run: `npm test -- homeassistant`
Expected: PASS.

- [ ] **Step 5: Commit (in gui-nightshift)**

```bash
cd gui-nightshift
git add src/lib/config/homeassistant.js src/lib/config/__tests__/homeassistant.test.js
git commit -m "feat(ha): connection helpers (status/connect/disconnect) + tests

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
cd ..
```

---

## Task 12: gui-nightshift — settings page + registration

**Files (in `gui-nightshift/`):**
- Create: `src/routes/settings/HomeAssistant.svelte`
- Test: `src/routes/settings/__tests__/HomeAssistant.test.js`
- Modify: `src/lib/config/pages.js`

- [ ] **Step 1: Inspect `src/lib/config/pages.js` and `src/routes/settings/Mqtt.svelte`**

Read both to copy the exact registration shape (route, icon, labelKey, section, component mapping) and the settings-page scaffold (Card, FormField, save-on-change). The HomeAssistant page mirrors `Mqtt.svelte`.

- [ ] **Step 2: Write the failing component test `src/routes/settings/__tests__/HomeAssistant.test.js`**

```js
// src/routes/settings/__tests__/HomeAssistant.test.js
import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import HomeAssistant from '../HomeAssistant.svelte'

// Mock the connection helpers so the component renders without a backend.
vi.mock('../../lib/config/homeassistant.js', () => ({
  isHaConnected: (s) => !!(s && s.connected),
  startHaAuth: vi.fn(),
  fetchHaStatus: vi.fn().mockResolvedValue({ enabled: true, connected: false, ha_url: '' }),
  disconnectHa: vi.fn().mockResolvedValue(true),
}))

describe('HomeAssistant settings page', () => {
  it('renders a URL field and a Connect control', async () => {
    render(HomeAssistant)
    expect(await screen.findByLabelText(/home assistant url/i)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /connect/i })).toBeInTheDocument()
  })
})
```

- [ ] **Step 3: Run to verify failure**

Run: `npm test -- HomeAssistant`
Expected: FAIL — cannot resolve `../HomeAssistant.svelte`.

- [ ] **Step 4: Implement `src/routes/settings/HomeAssistant.svelte`**

Use the existing settings-page conventions (Card, the config_store, FormField/PasswordInput components, `svelte-i18n`). Concrete starting implementation:

```svelte
<!-- src/routes/settings/HomeAssistant.svelte -->
<script>
  import { _ } from 'svelte-i18n'
  import { onMount } from 'svelte'
  import Card from '../../lib/components/ui/Card.svelte'
  import { config_store } from '../../lib/stores/config.js'
  import {
    isHaConnected, startHaAuth, fetchHaStatus, disconnectHa,
  } from '../../lib/config/homeassistant.js'

  let url = ''
  let status = null
  let saving = false

  $: connected = isHaConnected(status)

  onMount(async () => {
    const cfg = $config_store
    url = (cfg && cfg.ha_url) || ''
    status = await fetchHaStatus()
  })

  async function saveUrl() {
    saving = true
    await config_store.saveParam('ha_url', url)
    saving = false
    status = await fetchHaStatus()
  }

  async function onConnect() {
    await saveUrl()      // ensure the URL is persisted before redirecting
    startHaAuth()
  }

  async function onDisconnect() {
    await disconnectHa()
    status = await fetchHaStatus()
  }
</script>

<section class="p-4">
  <h1 class="mb-4 text-lg font-semibold text-text">{$_('config.homeassistant.title')}</h1>
  <Card class="mb-4 p-4 space-y-4">
    <label class="block">
      <span class="text-sm text-text-dim">{$_('config.homeassistant.url')}</span>
      <input
        class="mt-1 w-full rounded border border-border bg-surface p-2 text-text"
        type="url"
        aria-label={$_('config.homeassistant.url')}
        bind:value={url}
        placeholder="http://homeassistant.local:8123"
        on:change={saveUrl}
      />
    </label>

    <div class="flex items-center gap-3">
      <span class="text-sm">
        {connected ? $_('config.homeassistant.connected') : $_('config.homeassistant.disconnected')}
      </span>
      {#if connected}
        <button class="rounded bg-surface px-3 py-1 text-sm" on:click={onDisconnect}>
          {$_('config.homeassistant.disconnect')}
        </button>
      {:else}
        <button
          class="rounded bg-accent px-3 py-1 text-sm text-black disabled:opacity-50"
          disabled={!url || saving}
          on:click={onConnect}
        >
          {$_('config.homeassistant.connect')}
        </button>
      {/if}
    </div>
  </Card>
</section>
```

Add the i18n strings to the locale file(s) the app uses (find where `config.sections.*` keys live — likely `src/lib/i18n/en.json` or similar; add a `config.homeassistant` block: `title`, `url`, `connect`, `disconnect`, `connected`, `disconnected`). Match the existing locale file structure exactly.

- [ ] **Step 5: Register the page in `src/lib/config/pages.js`**

Add an entry to the settings pages list following the existing shape (copy a sibling entry such as the MQTT one and adjust):

```js
// within the pages array, in the appropriate section group:
{
  route: 'settings/home-assistant',
  component: HomeAssistant,            // import at top of pages.js
  icon: 'mdi:home-assistant',
  labelKey: 'config.homeassistant.title',
  section: 'integrations',             // use whatever section MQTT/Tesla use
},
```

Add the matching import at the top of `pages.js`:

```js
import HomeAssistant from '../../routes/settings/HomeAssistant.svelte'
```

(Confirm the exact path-depth and the section key by copying the sibling MQTT/Tesla registration.)

- [ ] **Step 6: Run the component test + full suite**

Run: `npm test -- HomeAssistant`
Expected: PASS.

Run: `npm test`
Expected: PASS — no existing tests regressed.

- [ ] **Step 7: Build the GUI to confirm it compiles**

Run: `npm run build`
Expected: SUCCESS.

- [ ] **Step 8: Commit (in gui-nightshift)**

```bash
cd gui-nightshift
git add src/routes/settings/HomeAssistant.svelte \
        src/routes/settings/__tests__/HomeAssistant.test.js \
        src/lib/config/pages.js src/lib/i18n/
git commit -m "feat(ha): Home Assistant settings page + nav registration

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
cd ..
```

---

## Task 13: Full build + hardware validation

**Files:** none (verification only)

- [ ] **Step 1: Build the P4 firmware (core-3) with the GUI bundled**

Run: `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4`
Expected: SUCCESS. (Confirm the exact `GUI_NAME` mechanism from `docs`/memory `p4-ota-and-custom-gui-build`; bundle the freshly built `gui-nightshift/dist`.)

- [ ] **Step 2: Build a core-2 env as a regression check**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 3: Native tests green**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 4: Flash + on-hardware click-through (manual)**

Flash the P4 (`--upload-port /dev/ttyACM5`), then verify against a real HA instance via the UI / `/ha/status` (serial console is not readable from the build host):
- Set HA URL on the settings page; click **Connect**; complete HA login in the browser; confirm redirect back to the settings page shows **Connected** and `GET /ha/status` returns `connected:true`.
- Reboot the EVSE; confirm `/ha/status` still `connected:true` (refresh-token persistence).
- Click **Disconnect**; confirm `/ha/status` `connected:false` and the `ha_*` secrets are cleared in `/config`.
- In HA, revoke the token (Profile → Security); wait for the next refresh cycle; confirm `/ha/status` flips to `connected:false`.

- [ ] **Step 5: Commit any fixes found during validation, then push the branch**

```bash
git push -u origin feature/home-assistant-oauth
# In gui-nightshift, push its branch too if the submodule tracks one.
```

---

## Notes for the implementer

- **Async HTTP:** `MongooseHttpClient` callbacks (`onResponse`) fire later on the Mongoose poll loop; never block waiting for them. State transitions happen inside the callback (see `storeTokens`).
- **`config_commit()` name:** verify the exact persist function by grepping how `web_server_config.cpp` saves after a POST `/config`; use that call everywhere this plan writes `config_commit()`.
- **`requestPreProcess` + redirect freeing:** copy the exact idiom from an existing redirecting handler in `web_server.cpp`; the `delete response` line in Task 10 is a placeholder for "follow the existing pattern."
- **`request->isSecure()`:** if no such accessor exists, default `secure=false` for v1 (LAN http). The device scheme only affects the `client_id`/`redirect_uri` it advertises; revisit when the EVSE is served over TLS.
- **No PKCE:** HA's auth flow is used without PKCE here. If a future HA version requires it, add `code_challenge` (S256) at authorize and `code_verifier` at exchange in `ha_oauth` (unit-testable) — out of scope for v1.
