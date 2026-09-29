#include "emonesp.h"
#include "crash_upload.h"

#if ENABLE_CRASH_UPLOAD

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MongooseCore.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include "crash_host.h"
#include "crash_payload.h"
#include "crash_report_id.h"
#include "diagnostics.h"
#include "event.h"
#include "net_manager.h"
#include "ota_url_allow.h"     // crash_url_host_allowed()

// Fed to the connection in slices as its buffer drains, rather than handed
// over whole: the point of diagnostics_coredump_image() is that the 64 KB
// never enters the heap, and a single mg_send() of the lot would copy all of
// it into the send buffer and undo that.
#define CRASH_CHUNK              1024
#define CRASH_HIGH_WATER         4096

// The whole exchange, not one request. A charger that cannot finish in a
// minute is one whose network is the problem, and holding a TLS session open
// past that costs more heap than it is worth.
#define CRASH_TIMEOUT_MS         60000

// Broker replies are a few hundred bytes. A reply that keeps growing is either
// a bug or a hostile endpoint filling the heap of the device it is talking to.
#define CRASH_MAX_REPLY          8192

// A TLS handshake needs tens of KB contiguous, and heap_largest collapses on a
// charger that has been up for weeks -- the documented reason OTA fails then
// and succeeds after a reboot. Below this, an upload defers to tier 2 rather
// than failing in the middle of a handshake, and a deletion asks for a retry.
//
// Measured on a no-PSRAM WROOM (openevse_wifi_tft_v1_dev): one request dips
// the largest free block by ~30 KB at its peak, and the first TLS use of a boot
// leaves it ~8 KB lower for good (55.3 KB -> 47.1 KB). At the earlier
// provisional 48 KB, deleting right after sending was always refused; 40 KB
// keeps ~10 KB of margin over the measured peak.
#ifndef CRASH_MIN_HEAP_LARGEST
#define CRASH_MIN_HEAP_LARGEST   (40 * 1024)
#endif

// Set by a click, consumed once, never re-armed by firmware (spec §8).
//
// A LittleFS file, where spec §6.1 says NVS. Same durability, and a file is
// reachable from exactly one place in this firmware; nothing on master writes
// an arbitrary LittleFS path from a request. A config option -- the obvious
// alternative -- could be set by any authenticated /config write, which would
// make the consent invariant a comment rather than a property.
#define CRASH_DEFER_FLAG         "/crash_upload_pending"

// The reporter identity (crash_report_id.h): random id + delete key. A file,
// not a config option, for the same reason as the defer flag -- and so the key
// never appears in GET /config.
#define CRASH_IDENTITY_FILE      "/crash_reporter"

static CrashUploadState _state = CrashUpload_Idle;

// Erasure runs on the same connection machinery as an upload, but its outcome
// is its own: a deletion must not read as an upload failing or finishing.
enum CrashForget { CrashForget_Idle, CrashForget_Running, CrashForget_Deleted, CrashForget_Failed };
static CrashForget _forget = CrashForget_Idle;
static String _forgetBody;           // holds the key only while the request runs
static uint32_t _forgetDeleted = 0;
static String _reportId;
static const uint8_t *_image = NULL;
static size_t _imageLen = 0;
static size_t _sent = 0;
static size_t _reported = 0;       // last progress figure announced
static uint32_t _deadline = 0;
static bool _triedThisBoot = false;
// The flag was on disk when this boot started. Only such a flag is a deferred
// upload; one armed during this boot belongs to the next one.
static bool _armedAtBoot = false;

// The step to start on the next crash_upload_loop(), after Mongoose.poll() has
// destroyed the connection that just answered. Starting it from inside that
// connection's reply handler would allocate a second TLS context while the
// first still holds its buffers -- the overlap main.cpp already avoids for OTA
// redirects, and on a no-PSRAM board roughly double the peak the heap gate was
// sized for.
enum CrashNext { CrashNext_None, CrashNext_Raw, CrashNext_Complete };
static CrashNext _next = CrashNext_None;

// ---------------------------------------------------------------------------
// One request, used three times.
// ---------------------------------------------------------------------------

struct CrashRequest {
  const char *method;
  String path;
  const char *contentType;
  const uint8_t *body;
  size_t bodyLen;
  size_t sent;
  bool headSent;
  String reply;
  int status;
};

static CrashRequest _req;
static mg_connection *_nc = NULL;

static void crash_fail(const char *why);
static void crash_reply(int status, const String &body);

static void crash_pump(mg_connection *nc)
{
  while(_req.sent < _req.bodyLen && nc->send_mbuf.len < CRASH_HIGH_WATER) {
    size_t chunk = _req.bodyLen - _req.sent;
    if(chunk > CRASH_CHUNK) {
      chunk = CRASH_CHUNK;
    }
    mg_send(nc, _req.body + _req.sent, chunk);
    _req.sent += chunk;
  }
  if(CrashUpload_Raw == _state) {
    _sent = _req.sent;
    // Announced every 8 KB rather than every slice: this runs from the
    // mongoose poll, and a websocket broadcast per kilobyte would cost more
    // than the upload.
    if(_sent - _reported >= 8192 || _sent == _imageLen) {
      _reported = _sent;
      DynamicJsonDocument doc(128);
      doc["crash_upload"] = crash_upload_state_name();
      doc["crash_upload_sent"] = (uint32_t)_sent;
      doc["crash_upload_total"] = (uint32_t)_imageLen;
      event_send(doc);
    }
  }
}

static void crash_ev_handler(struct mg_connection *nc, int ev, void *p, void *u)
{
  (void)u;
  // One request struct serves three successive connections. A straggling
  // MG_EV_SEND on a connection we have already moved past would otherwise pump
  // the NEXT request's body into the old socket.
  if(nc != _nc && MG_EV_CLOSE != ev) {
    return;
  }
  switch(ev)
  {
    case MG_EV_CONNECT:
      if(0 != *(int *)p) {
        crash_fail("connect failed");
        return;
      }
      // Request line and headers first, then the body in slices. Connection:
      // close because this is one request per connection and the broker's
      // reply is the end of it.
      mg_printf(nc,
                "%s %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: OpenEVSE\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                _req.method, _req.path.c_str(), CRASH_BROKER_HOST,
                _req.contentType, (unsigned)_req.bodyLen);
      _req.headSent = true;
      crash_pump(nc);
      break;

    case MG_EV_SEND:
    case MG_EV_POLL:
      if(_req.headSent) {
        crash_pump(nc);
      }
      break;

    case MG_EV_RECV:
      if(nc->recv_mbuf.len > CRASH_MAX_REPLY) {
        crash_fail("reply too large");
      }
      break;

    case MG_EV_HTTP_REPLY: {
      http_message *hm = (http_message *)p;
      String body;
      body.reserve(hm->body.len + 1);
      body.concat(hm->body.p, hm->body.len);
      int status = hm->resp_code;
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      _nc = NULL;
      crash_reply(status, body);
      break;
    }

    case MG_EV_CLOSE:
      if(_nc == nc) {
        _nc = NULL;
        crash_fail("closed before a reply");
      }
      break;
  }
}

// Start one request. `body` must outlive the request -- for the raw PUT that
// is the flash mapping, which is valid for the life of the boot.
static bool crash_send(const char *method, const String &path,
                       const char *contentType,
                       const uint8_t *body, size_t bodyLen)
{
  String url = String("https://") + CRASH_BROKER_HOST + path;
  // Spec §4, on every request rather than only the first: a compromised or
  // spoofed broker reply cannot move the image off this host.
  if(!crash_url_host_allowed(url.c_str())) {
    crash_fail("destination not allowed");
    return false;
  }

  _req.method = method;
  _req.path = path;
  _req.contentType = contentType;
  _req.body = body;
  _req.bodyLen = bodyLen;
  _req.sent = 0;
  _req.headSent = false;
  _req.status = 0;

  struct mg_connect_opts opts;
  // `true`: this asks for the real root CA bundle. MongooseHttpClient passes
  // false and mongoose then substitutes "*", which is no verification at all
  // (mongoose.c:8672) -- not something to inherit on a path that carries a
  // memory image.
  Mongoose.getDefaultOpts(&opts, true);
  const char *err = NULL;
  opts.error_string = &err;

  // "tcp://", NOT "ssl://" -- mg_parse_address strips only udp:// and tcp://
  // (mongoose.c:2648), so an ssl:// address fails to parse and mg_connect_opt
  // returns NULL with "cannot parse address". TLS is turned on by opts
  // .ssl_ca_cert being non-NULL, which is exactly what mg_connect_http_base
  // does (mongoose.c:8660). SNI and hostname verification come free: with a CA
  // set and no ssl_server_name, mongoose uses the DNS host it parsed
  // (mongoose.c:3158) and mbedtls_ssl_set_hostname checks the cert against it.
  String addr = String("tcp://") + CRASH_BROKER_HOST + ":443";
  // Asynchronous, including the DNS lookup: mongoose resolves through its own
  // resolver rather than getaddrinfo, so nothing here blocks loopTask
  // (spec §6.4).
  _nc = mg_connect_opt(Mongoose.getMgr(), addr.c_str(),
                       MG_CB(crash_ev_handler, NULL), opts);
  if(NULL == _nc) {
    crash_fail(err ? err : "connect failed");
    return false;
  }
  mg_set_protocol_http_websocket(_nc);
  return true;
}

// ---------------------------------------------------------------------------
// The three steps.
// ---------------------------------------------------------------------------

static String _metaBody;      // must outlive the request

static bool crash_identity_load(char rid[33], char key[65])
{
  if(!LittleFS.exists(CRASH_IDENTITY_FILE)) {
    return false;
  }
  File f = LittleFS.open(CRASH_IDENTITY_FILE, "r");
  if(!f) {
    return false;
  }
  String text = f.readString();
  f.close();
  return crash_identity_parse(text.c_str(), rid, key);
}

static bool crash_identity_create(char rid[33], char key[65])
{
  // Only ever called with the network up, so the RF noise source is running
  // and esp_fill_random is a true RNG.
  uint8_t r[CRASH_REPORTER_ID_HEX / 2];
  uint8_t k[CRASH_DELETE_KEY_HEX / 2];
  esp_fill_random(r, sizeof(r));
  esp_fill_random(k, sizeof(k));
  crash_hex(r, sizeof(r), rid);
  crash_hex(k, sizeof(k), key);
  char text[CRASH_IDENTITY_LEN];
  crash_identity_format(rid, key, text, sizeof(text));
  File f = LittleFS.open(CRASH_IDENTITY_FILE, "w");
  if(!f) {
    return false;
  }
  size_t n = f.print(text);
  f.close();
  return n == strlen(text);
}

static void crash_step_metadata()
{
  // Created on first use, so a charger that never sends a report never has
  // one. If it cannot be stored, nothing is sent: a report without a stored
  // key is one the user could never erase.
  char rid[33], key[65], keyHash[65];
  if(!crash_identity_load(rid, key) && !crash_identity_create(rid, key)) {
    crash_fail("could not store the reporter id");
    return;
  }
  crash_delete_key_hash(key, keyHash);

  DynamicJsonDocument doc(6144);
  crash_payload_build(doc, crash_declared_raw_bytes(_imageLen), rid, keyHash);
  _metaBody = "";
  serializeJson(doc, _metaBody);

  _state = CrashUpload_Metadata;
  crash_send("POST", "/v1/reports", "application/json",
             (const uint8_t *)_metaBody.c_str(), _metaBody.length());
}

static void crash_step_raw()
{
  _state = CrashUpload_Raw;
  _sent = 0;
  _reported = 0;
  crash_send("PUT", "/v1/reports/" + _reportId + "/raw",
             "application/octet-stream", _image, _imageLen);
}

static void crash_step_complete()
{
  _state = CrashUpload_Completing;
  crash_send("POST", "/v1/reports/" + _reportId + "/complete",
             "application/json", (const uint8_t *)"", 0);
}

// The broker holds everything it was promised; erasing is safe now.
static void crash_finish()
{
  diagnostics_coredump_erase();
  _image = NULL;
  _state = CrashUpload_Done;
  DynamicJsonDocument doc(128);
  doc["crash_upload"] = crash_upload_state_name();
  event_send(doc);
}

static void crash_forget_reply(int status, const String &body)
{
  _forgetBody = "";
  if(200 != status) {
    crash_fail("the broker refused the deletion");
    return;
  }
  StaticJsonDocument<32> filter;
  filter["deleted"] = true;
  StaticJsonDocument<64> doc;
  deserializeJson(doc, body, DeserializationOption::Filter(filter));
  _forgetDeleted = doc["deleted"] | 0;
  // A fresh identity next time, so a later report is not linkable to the ones
  // just erased.
  LittleFS.remove(CRASH_IDENTITY_FILE);
  _forget = CrashForget_Deleted;
  DynamicJsonDocument ev(128);
  ev["crash_forget"] = crash_forget_state_name();
  ev["crash_forget_deleted"] = _forgetDeleted;
  event_send(ev);
}

static void crash_reply(int status, const String &body)
{
  if(CrashForget_Running == _forget) {
    crash_forget_reply(status, body);
    return;
  }
  switch(_state)
  {
    case CrashUpload_Metadata: {
      if(200 != status) {
        crash_fail("broker refused the report");
        return;
      }
      // Filtered, not parsed whole. A summary-only report comes back with its
      // symbolized `frames` (a few KB, bounded by CRASH_MAX_REPLY), and none
      // of that is for the device: only report_id survives the filter.
      StaticJsonDocument<64> filter;
      filter["report_id"] = true;
      StaticJsonDocument<128> doc;
      if(DeserializationError::Ok !=
         deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        crash_fail("bad reply");
        return;
      }
      const char *id = doc["report_id"];
      // Review Focus 1. The id is validated and the path is rebuilt from it;
      // the broker's own suggested path is never used, so no string from the
      // network reaches a request line.
      if(!crash_report_id_valid(id)) {
        crash_fail("bad report id");
        return;
      }
      _reportId = id;
      if(crash_declared_raw_bytes(_imageLen) > 0) {
        _next = CrashNext_Raw;
      } else {
        // Summary only: the broker marked the report complete when it indexed
        // it, so this 200 is the whole upload.
        crash_finish();
      }
      break;
    }

    case CrashUpload_Raw:
      if(200 != status) {
        crash_fail("dump refused");
        return;
      }
      _next = CrashNext_Complete;
      break;

    case CrashUpload_Completing:
      if(200 != status) {
        crash_fail("completion refused");
        return;
      }
      // Spec §7: both the PUT and the completion answered 2xx, and only now is
      // erasing safe. A dump erased on a partial upload is unrecoverable.
      crash_finish();
      break;

    default:
      break;
  }
}

static void crash_fail(const char *why)
{
  DBUGF("crash upload failed: %s", why);
  if(_nc) {
    _nc->flags |= MG_F_CLOSE_IMMEDIATELY;
    _nc = NULL;
  }
  if(CrashForget_Running == _forget) {
    // The identity stays, so the user can simply try again.
    _forgetBody = "";
    _forget = CrashForget_Failed;
    DynamicJsonDocument doc(192);
    doc["crash_forget"] = crash_forget_state_name();
    doc["crash_forget_error"] = why;
    event_send(doc);
    return;
  }
  _state = CrashUpload_Failed;
  _next = CrashNext_None;
  _image = NULL;
  DynamicJsonDocument doc(192);
  doc["crash_upload"] = crash_upload_state_name();
  doc["crash_upload_error"] = why;
  event_send(doc);
}

// ---------------------------------------------------------------------------
// Public surface.
// ---------------------------------------------------------------------------

static bool crash_running()
{
  return CrashUpload_Metadata == _state || CrashUpload_Raw == _state ||
         CrashUpload_Completing == _state || CrashForget_Running == _forget;
}

// The first 64 bits of the stored image's SHA-256, or false if there is none:
// the identity a deferred click consented to (spec section 8). See
// crash_report_id.h for why this cannot be a CRC32.
static bool crash_dump_identity(uint64_t *id, size_t *len)
{
  const uint8_t *img = NULL;
  if(!diagnostics_coredump_image(&img, len) || 0 == *len) {
    return false;
  }
  uint8_t digest[32];
  if(0 != mbedtls_sha256(img, *len, digest, 0)) {
    return false;
  }
  *id = 0;
  for(int i = 0; i < 8; i++) {
    *id = (*id << 8) | digest[i];
  }
  return true;
}

static void crash_arm_deferred()
{
  // The flag records WHICH dump was offered, not just that one was. If the
  // charger crashes again before the next boot, that newer dump was never
  // offered and must not go (spec section 8).
  uint64_t id = 0;
  size_t len = 0;
  char token[CRASH_DEFER_TOKEN_LEN] = "";
  if(crash_dump_identity(&id, &len)) {
    crash_defer_token(token, id, len);
  }
  File f = LittleFS.open(CRASH_DEFER_FLAG, "w");
  if(f) {
    f.print(token);
    f.close();
  }
  // Deferred means NEXT boot. Without this, crash_upload_loop() would see the
  // Deferred state on its very next pass, consume the flag and retry
  // immediately against the same starved heap that just failed the gate --
  // turning "will upload after the next restart" into a lie and losing the
  // flag in the process.
  _triedThisBoot = true;
  _state = CrashUpload_Deferred;
  DynamicJsonDocument doc(128);
  doc["crash_upload"] = crash_upload_state_name();
  event_send(doc);
}

bool crash_upload_deferred_armed()
{
  return LittleFS.exists(CRASH_DEFER_FLAG);
}

void crash_upload_cancel_deferred()
{
  LittleFS.remove(CRASH_DEFER_FLAG);
  _armedAtBoot = false;
  if(CrashUpload_Deferred == _state) {
    _state = CrashUpload_Idle;
  }
}

static bool crash_begin_now(String &message)
{
  if(!diagnostics_coredump_image(&_image, &_imageLen) || 0 == _imageLen) {
    message = F("no crash dump stored");
    return false;
  }
  _deadline = millis() + CRASH_TIMEOUT_MS;
  _triedThisBoot = true;
  crash_step_metadata();
  message = F("uploading");
  return true;
}

bool crash_upload_request(String &message)
{
  if(crash_running()) {
    // Two uploaders would share one mapping, one request struct and one
    // connection pointer. Refusing is the whole guard (Review Focus 3).
    message = F("an upload is already running");
    return false;
  }

  const uint8_t *img = NULL;
  size_t len = 0;
  if(!diagnostics_coredump_image(&img, &len) || 0 == len) {
    message = F("no crash dump stored");
    return false;
  }

  if(!net.isConnected()) {
    crash_arm_deferred();
    message = F("no network -- will upload after the next restart");
    return true;
  }

  uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if(largest < CRASH_MIN_HEAP_LARGEST) {
    // Spec §6.1 tier 2. Not a failure: the click stands, and the upload
    // completes on the next boot against a pristine heap. Telling the user it
    // worked would be a lie, and telling them it failed would make them think
    // the dump was lost.
    DBUGF("crash upload deferred, largest free block %u", largest);
    crash_arm_deferred();
    message = F("not enough contiguous memory -- will upload after the next restart");
    return true;
  }

  return crash_begin_now(message);
}

void crash_upload_begin()
{
  // Nothing here touches the network; tier 2 waits for connectivity in loop().
  if(crash_upload_deferred_armed()) {
    _armedAtBoot = true;
    _state = CrashUpload_Deferred;
  }
}

void crash_upload_loop()
{
  if(CrashNext_None != _next) {
    // Called after Mongoose.poll(), so the previous step's connection -- and
    // its TLS context -- is gone before this one allocates.
    CrashNext n = _next;
    _next = CrashNext_None;
    if(CrashNext_Raw == n) {
      crash_step_raw();
    } else {
      crash_step_complete();
    }
    return;
  }

  if(crash_running()) {
    if((long)(millis() - _deadline) >= 0) {
      crash_fail("timed out");
    }
    return;
  }

  if(CrashUpload_Deferred != _state || _triedThisBoot || !net.isConnected() ||
     !_armedAtBoot) {
    return;
  }

  // Spec §6.1 tier 2, and deviation D3: this runs at the first moment the
  // network is up, which races MQTT and OCPP rather than strictly preceding
  // them. The spec's "before MQTT connects" needs a hook inside the connect
  // path, and that is the code path #1252 came out of. Tier 3 (spec §6.3) is
  // the real answer and stays deferred.
  //
  // The flag is cleared BEFORE the attempt, not after: a crash mid-upload must
  // not boot back into uploading, which on a device that is not charging is a
  // loop. A failed attempt leaves the dump in place and the user can click
  // again.
  String offered;
  {
    File f = LittleFS.open(CRASH_DEFER_FLAG, "r");
    if(f) {
      offered = f.readString();
      f.close();
    }
  }
  LittleFS.remove(CRASH_DEFER_FLAG);

  // Spec section 8: only the dump the click offered. A crash after the click
  // leaves a different dump here, and nobody offered that one.
  uint64_t id = 0;
  size_t len = 0;
  if(!crash_dump_identity(&id, &len) ||
     !crash_defer_token_matches(offered.c_str(), id, len)) {
    _triedThisBoot = true;
    _armedAtBoot = false;
    _state = CrashUpload_Failed;
    DynamicJsonDocument doc(160);
    doc["crash_upload"] = crash_upload_state_name();
    doc["crash_upload_error"] = "the stored crash is not the one that was offered";
    event_send(doc);
    return;
  }

  // Set before the attempt, not after. crash_begin_now() returns false when
  // the dump has since been erased, and without this the loop would call it --
  // and esp_core_dump_image_get(), a flash read -- on every pass for the rest
  // of the boot.
  _triedThisBoot = true;
  String message;
  if(!crash_begin_now(message)) {
    _state = CrashUpload_Idle;
  }
}

bool crash_reporter_id(char out[33])
{
  char key[65];
  bool ok = crash_identity_load(out, key);
  memset(key, 0, sizeof(key));
  return ok;
}

bool crash_forget_request(String &message)
{
  if(crash_running()) {
    message = F("an upload is running -- try again when it has finished");
    return false;
  }
  // Withdrawing consent covers the report that has not gone yet, too.
  crash_upload_cancel_deferred();

  char rid[33], key[65];
  if(!crash_identity_load(rid, key)) {
    message = F("nothing has been sent from this charger");
    return false;
  }
  if(!net.isConnected()) {
    message = F("no network");
    return false;
  }
  uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if(largest < CRASH_MIN_HEAP_LARGEST) {
    message = F("not enough free memory right now -- try again after a restart");
    return false;
  }

  _forgetBody = String("{\"delete_key\":\"") + key + "\"}";
  memset(key, 0, sizeof(key));
  _forget = CrashForget_Running;
  _forgetDeleted = 0;
  _deadline = millis() + CRASH_TIMEOUT_MS;
  {
    DynamicJsonDocument doc(96);
    doc["crash_forget"] = crash_forget_state_name();
    event_send(doc);
  }
  if(!crash_send("POST", String("/v1/reporters/") + rid + "/delete",
                 "application/json", (const uint8_t *)_forgetBody.c_str(),
                 _forgetBody.length())) {
    message = F("could not reach OpenEVSE");
    return false;
  }
  message = F("deleting");
  return true;
}

uint32_t crash_forget_deleted() { return _forgetDeleted; }

const char *crash_forget_state_name()
{
  switch(_forget) {
    case CrashForget_Running: return "deleting";
    case CrashForget_Deleted: return "deleted";
    case CrashForget_Failed:  return "failed";
    default:                  return "idle";
  }
}

CrashUploadState crash_upload_state() { return _state; }
size_t crash_upload_sent() { return _sent; }
size_t crash_upload_total() { return _imageLen; }

const char *crash_upload_state_name()
{
  switch(_state) {
    case CrashUpload_Metadata:   return "metadata";
    case CrashUpload_Raw:        return "uploading";
    case CrashUpload_Completing: return "completing";
    case CrashUpload_Done:       return "done";
    case CrashUpload_Failed:     return "failed";
    case CrashUpload_Deferred:   return "deferred";
    default:                     return "idle";
  }
}

#else // ENABLE_CRASH_UPLOAD

// Built out on the 4 MB boards (spec §11). The callers stay unconditional so
// main.cpp and web_server.cpp do not grow a second set of #ifs.
void crash_upload_begin() {}
void crash_upload_loop() {}
bool crash_upload_request(String &message)
{
  message = F("not supported on this build");
  return false;
}
CrashUploadState crash_upload_state() { return CrashUpload_Idle; }
const char *crash_upload_state_name() { return "unsupported"; }
size_t crash_upload_sent() { return 0; }
size_t crash_upload_total() { return 0; }
bool crash_upload_deferred_armed() { return false; }
void crash_upload_cancel_deferred() {}
bool crash_forget_request(String &message)
{
  message = F("not supported on this build");
  return false;
}
const char *crash_forget_state_name() { return "unsupported"; }
uint32_t crash_forget_deleted() { return 0; }
bool crash_reporter_id(char out[33]) { out[0] = '\0'; return false; }

#endif // ENABLE_CRASH_UPLOAD
