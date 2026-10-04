#ifdef ENABLE_DEBUG
#undef ENABLE_DEBUG
#endif

#include "debug.h"
#include "web_server.h"
#include "app_config.h"
#include "evse_man.h"
#include "input.h"

// -------------------------------------------------------------------
// Installer tools
//
//   POST /installer/verify     {"password"}                    -> check it
//   POST /installer/password   {"current","new"}               -> change it
//   POST /installer/maxcurrent {"password","amps"}             -> $SC amps M
//
// The password lives on the charger and is never sent to the browser, so the
// GUI cannot check it locally - it asks here. Each call sits behind the normal
// login (requestPreProcess) and the GUI-only header, and every call that takes
// a password shares one failed-attempt throttle.
//
// This is a commissioning gate, not strong authentication: the password is
// stored like www_password, and POST /config can still carry max_current_hard
// from any logged-in client. The controller itself is what makes the hardware
// limit write-once.
// -------------------------------------------------------------------

#define INSTALLER_MIN_PASSWORD_LEN   4
#define INSTALLER_MAX_PASSWORD_LEN   32
#define INSTALLER_MIN_AMPS           6
#define INSTALLER_MAX_AMPS           80
#define INSTALLER_MAX_FAILURES       5
#define INSTALLER_LOCKOUT_MS         30000UL

static uint8_t installer_failures = 0;
static unsigned long installer_locked_until = 0;
static bool installer_locked = false;

// Seconds left on the lockout, 0 when attempts are allowed.
static unsigned long installerLockoutRemaining()
{
  if(!installer_locked) {
    return 0;
  }
  long left = (long)(installer_locked_until - millis());
  if(left <= 0) {
    installer_locked = false;
    installer_failures = 0;
    return 0;
  }
  return (left + 999) / 1000;
}

static void installerFailure()
{
  if(++installer_failures >= INSTALLER_MAX_FAILURES) {
    installer_locked = true;
    installer_locked_until = millis() + INSTALLER_LOCKOUT_MS;
  }
}

static void installerReply(MongooseHttpServerRequest *request,
                           MongooseHttpServerResponseStream *response,
                           int code, const char *msg)
{
  response->setCode(code);
  response->printf("{\"msg\":\"%s\"}", msg);
  request->send(response);
}

// Common front matter: login, GUI-only header, POST, JSON body. Returns false
// once a reply has been sent.
static bool installerPrologue(MongooseHttpServerRequest *request,
                              MongooseHttpServerResponseStream *&response,
                              DynamicJsonDocument &in)
{
  if(false == requestPreProcess(request, response, CONTENT_TYPE_JSON)) {
    return false;
  }
  MongooseString xrw = request->headers("X-Requested-With");
  if(0 != strcmp(xrw.toString().c_str(), "OpenEVSE")) {
    installerReply(request, response, 403, "csrf");
    return false;
  }
  if(HTTP_POST != request->method()) {
    installerReply(request, response, 405, "method not allowed");
    return false;
  }
  if(deserializeJson(in, request->body().toString())) {
    installerReply(request, response, 400, "bad json");
    return false;
  }
  return true;
}

// Checks `candidate` against the stored password, applying the throttle.
// Returns 0 on a match, otherwise the HTTP code already replied with.
static int installerCheckPassword(MongooseHttpServerRequest *request,
                                  MongooseHttpServerResponseStream *response,
                                  const char *candidate)
{
  unsigned long wait = installerLockoutRemaining();
  if(wait > 0) {
    response->setCode(429);
    response->printf("{\"msg\":\"locked\",\"retry_after\":%lu}", wait);
    request->send(response);
    return 429;
  }
  if(credentialsMatch(config_installer_password().c_str(), candidate)) {
    installer_failures = 0;
    return 0;
  }
  installerFailure();
  installerReply(request, response, 403, "wrong password");
  return 403;
}

void handleInstallerVerify(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  DynamicJsonDocument in(256);
  if(!installerPrologue(request, response, in)) {
    return;
  }
  if(installerCheckPassword(request, response, in["password"] | "")) {
    return;
  }
  installerReply(request, response, 200, "ok");
}

void handleInstallerPassword(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  DynamicJsonDocument in(256);
  if(!installerPrologue(request, response, in)) {
    return;
  }
  if(installerCheckPassword(request, response, in["current"] | "")) {
    return;
  }

  const char *next = in["new"] | "";
  size_t len = strlen(next);
  bool valid = len >= INSTALLER_MIN_PASSWORD_LEN && len <= INSTALLER_MAX_PASSWORD_LEN;
  for(size_t i = 0; valid && i < len; i++) {
    // Printable ASCII only, so it survives every transport it might take.
    valid = next[i] >= 0x20 && next[i] < 0x7f;
  }
  if(!valid) {
    installerReply(request, response, 400, "invalid password");
    return;
  }

  if(!config_installer_password_set(next)) {
    installerReply(request, response, 500, "error");
    return;
  }
  installerReply(request, response, 200, "changed");
}

void handleInstallerMaxCurrent(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  DynamicJsonDocument in(256);
  if(!installerPrologue(request, response, in)) {
    return;
  }
  if(installerCheckPassword(request, response, in["password"] | "")) {
    return;
  }

  // The valid range is what the controller will actually write. Its setter
  // clamps to [min current, current hardware maximum] rather than refusing,
  // and the write is once only: a request outside that range would spend it
  // on a value nobody asked for. So refuse here instead -- in practice the
  // limit can only come down.
  long previous = evse.getMaxHardwareCurrent();
  long lo = max((long)INSTALLER_MIN_AMPS, evse.getMinCurrent());
  long hi = previous > 0 ? min((long)INSTALLER_MAX_AMPS, previous) : (long)INSTALLER_MAX_AMPS;

  long amps = in["amps"] | 0L;
  if(amps < lo || amps > hi) {
    response->setCode(400);
    response->printf("{\"msg\":\"amps out of range\",\"min\":%ld,\"max\":%ld}", lo, hi);
    request->send(response);
    return;
  }

  // OpenEVSE_Lib: setCurrentCapacityFactoryLimit() -> "$SC <amps> M". The
  // controller accepts it once; later writes return $NK and are ignored, so the
  // reply only says the command was sent. The GUI re-reads /config to see
  // whether max_current_hard took the new value.
  evse.setMaxHardwareCurrent(amps);

  response->setCode(200);
  response->printf("{\"msg\":\"sent\",\"requested\":%ld,\"previous\":%ld}", amps, previous);
  request->send(response);
  DBUGF("Installer set hardware max current: %ld (was %ld)", amps, previous);
}
