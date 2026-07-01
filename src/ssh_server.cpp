#ifdef ENABLE_SSH_CLI

#include "ssh_server.h"
#include "app_config.h"
#include "espal.h"
#include "input.h" // extern EvseManager evse;
#include "cli/cli_engine.h"
#include <libssh/server.h>
#include <libssh/pki.h>
#include <libssh_esp32.h>
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

// Below this, refuse new SSH connections outright rather than risk an OOM —
// LibSSH-ESP32 session setup costs roughly 20-40KB of heap on top of this.
#define SSH_MIN_FREE_HEAP_TO_ACCEPT (40 * 1024)
#define SSH_TASK_STACK_WORDS        10240
#define SSH_TASK_PRIORITY           (tskIDLE_PRIORITY + 3)

// Its own file rather than a user_config field: the host key is a ~400+
// byte PEM-armored blob, and user_config is backed by a single fixed-size
// (CONFIG_SIZE, 3072 bytes) EEPROM-emulated region shared by ~90 other
// fields — ConfigJson::commit() bounds-checks writes against that fixed
// buffer and silently drops whatever doesn't fit, with no error reported.
// That's almost certainly why an earlier version of this (storing it as a
// config opt) didn't reliably survive reboots.
#define SSH_HOST_KEY_FILE "/ssh_host_key.pem"

SshServerTask sshServer(evse);

class SshChannelOutput : public CliOutput {
  public:
    SshChannelOutput(ssh_channel chan) : _chan(chan) { }
    void print(const char *s) override {
      ssh_channel_write(_chan, s, strlen(s));
    }
  private:
    ssh_channel _chan;
};

SshServerTask::SshServerTask(EvseManager &evse) :
  _evse(evse),
  _sshTaskHandle(nullptr),
  _cmdQueue(nullptr),
  _started(false),
  _hostKey(nullptr)
{
}

void SshServerTask::begin()
{
  MicroTask.startTask(this);
}

void SshServerTask::notifyConfigChanged()
{
  // v1: credential/enable changes are picked up on next boot. Hot
  // start/stop of the libssh listener while a session may be in flight is
  // deferred — flagged as a known v1 limitation in the SSH CLI plan.
  DBUGLN("SshServerTask: config changed, will take effect after reboot");
}

void SshServerTask::setup()
{
  _cmdQueue = xQueueCreate(1, sizeof(CliCmdRequest));

  if(!config_ssh_enabled()) {
    DBUGLN("SshServerTask: ssh_enabled is false, not starting listener");
    return;
  }

  // Done here (main thread, via MicroTasks setup()) rather than in
  // runServer() — loadOrCreateHostKey() calls config_set()/config_commit(),
  // which must never be touched from the dedicated SSH FreeRTOS task.
  _hostKey = loadOrCreateHostKey();
  if(!_hostKey) {
    DBUGLN("SshServerTask: failed to load/generate host key, sshd not started");
    return;
  }

  libssh_begin();
  xTaskCreatePinnedToCore(sshTaskEntry, "sshd", SSH_TASK_STACK_WORDS, this,
    SSH_TASK_PRIORITY, &_sshTaskHandle, portNUM_PROCESSORS - 1);
  _started = true;
}

unsigned long SshServerTask::loop(MicroTasks::WakeReason reason)
{
  CliCmdRequest req;
  while(_cmdQueue && pdTRUE == xQueueReceive(_cmdQueue, &req, 0)) {
    req.handler(*req.out, req.argc, req.argv);
    xSemaphoreGive(req.done);
  }
  return 20; // ms - poll often enough to keep the CLI responsive without busy-looping
}

void SshServerTask::execute(CliHandler handler, int argc, const char *argv[], CliOutput &out)
{
  CliCmdRequest req;
  req.handler = handler;
  req.argc = argc;
  req.out = &out;
  for(int i = 0; i < argc && i <= CLI_MAX_TOKENS; i++) {
    req.argv[i] = argv[i];
  }
  req.done = xSemaphoreCreateBinary();

  xQueueSend(_cmdQueue, &req, portMAX_DELAY);
  xSemaphoreTake(req.done, portMAX_DELAY);
  vSemaphoreDelete(req.done);
}

ssh_key SshServerTask::loadOrCreateHostKey()
{
  ssh_key key = nullptr;

  File in = LittleFS.open(SSH_HOST_KEY_FILE, "r");
  if(in) {
    String saved = in.readString();
    in.close();
    if(saved.length() > 0 &&
       SSH_OK == ssh_pki_import_privkey_base64(saved.c_str(), nullptr, nullptr, nullptr, &key)) {
      return key;
    }
    DBUGLN("SshServerTask: stored host key failed to import, regenerating");
  }

  if(SSH_OK != ssh_pki_generate(SSH_KEYTYPE_ED25519, 256, &key)) {
    return nullptr;
  }

  char *b64 = nullptr;
  if(SSH_OK == ssh_pki_export_privkey_base64(key, nullptr, nullptr, nullptr, &b64) && b64) {
    File out = LittleFS.open(SSH_HOST_KEY_FILE, "w");
    if(out) {
      out.print(b64);
      out.close();
    } else {
      DBUGLN("SshServerTask: failed to open host key file for writing");
    }
    free(b64);
  }
  return key;
}

bool SshServerTask::authenticate(ssh_session session)
{
  ssh_message message;
  do {
    message = ssh_message_get(session);
    if(!message) break;

    if(SSH_REQUEST_AUTH == ssh_message_type(message) &&
       SSH_AUTH_METHOD_PASSWORD == ssh_message_subtype(message)) {
      const char *user = ssh_message_auth_user(message);
      const char *pass = ssh_message_auth_password(message);
      if(ssh_username.length() > 0 && ssh_username.equals(user) && ssh_password.equals(pass)) {
        ssh_message_auth_reply_success(message, 0);
        ssh_message_free(message);
        return true;
      }
      ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_PASSWORD);
      ssh_message_reply_default(message);
    } else {
      ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_PASSWORD);
      ssh_message_reply_default(message);
    }
    ssh_message_free(message);
  } while(1);

  return false;
}

void SshServerTask::handleSession(ssh_session session)
{
  if(ssh_handle_key_exchange(session)) {
    return;
  }
  if(!authenticate(session)) {
    return;
  }

  ssh_channel chan = nullptr;
  ssh_message message;
  do {
    message = ssh_message_get(session);
    if(!message) return;
    if(SSH_REQUEST_CHANNEL_OPEN == ssh_message_type(message) &&
       SSH_CHANNEL_SESSION == ssh_message_subtype(message)) {
      chan = ssh_message_channel_request_open_reply_accept(message);
      ssh_message_free(message);
      break;
    }
    ssh_message_reply_default(message);
    ssh_message_free(message);
  } while(!chan);
  if(!chan) return;

  // Clients normally send pty-req (and often env/window-change) before the
  // shell request. We must ack those with success too — replying with the
  // default failure (as if we only understood "shell") makes well-behaved
  // clients believe no PTY was granted, so they fall back to local cooked-
  // mode echo on their end: Tab/arrow keys then show up as literal "^I" /
  // "^[[A" instead of being forwarded raw for us to interpret.
  bool shell = false;
  do {
    message = ssh_message_get(session);
    if(!message) break;
    if(SSH_REQUEST_CHANNEL == ssh_message_type(message)) {
      int subtype = ssh_message_subtype(message);
      if(SSH_CHANNEL_REQUEST_SHELL == subtype) {
        shell = true;
        ssh_message_channel_request_reply_success(message);
        ssh_message_free(message);
        break;
      }
      if(SSH_CHANNEL_REQUEST_PTY == subtype ||
         SSH_CHANNEL_REQUEST_ENV == subtype ||
         SSH_CHANNEL_REQUEST_WINDOW_CHANGE == subtype) {
        ssh_message_channel_request_reply_success(message);
        ssh_message_free(message);
        continue;
      }
    }
    ssh_message_reply_default(message);
    ssh_message_free(message);
  } while(!shell);
  if(!shell) {
    ssh_channel_close(chan);
    return;
  }

  SshChannelOutput out(chan);
  CliEngine engine(*this, esp_hostname.c_str());
  engine.printPrompt(out);

  uint8_t buf[256];
  bool closed = false;
  while(!closed) {
    int n = ssh_channel_read(chan, buf, sizeof(buf), 0);
    if(n <= 0) break;
    for(int i = 0; i < n && !closed; i++) {
      closed = engine.feedByte(buf[i], out);
    }
  }
  ssh_channel_close(chan);
}

void SshServerTask::runServer()
{
  ssh_bind sshbind = ssh_bind_new();
  ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_IMPORT_KEY, _hostKey);
  ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, "22");

  if(ssh_bind_listen(sshbind) < 0) {
    DBUGF("SshServerTask: ssh_bind_listen failed: %s", ssh_get_error(sshbind));
    ssh_bind_free(sshbind);
    vTaskDelete(NULL);
    return;
  }

  for(;;) {
    ssh_session session = ssh_new();
    int r = ssh_bind_accept(sshbind, session);
    if(SSH_ERROR == r) {
      ssh_free(session);
      continue;
    }

    // Single-session by construction: we don't call ssh_bind_accept() again
    // until handleSession() (below) has returned for this one.
    if(ESPAL.getFreeHeap() < SSH_MIN_FREE_HEAP_TO_ACCEPT) {
      ssh_disconnect(session);
      ssh_free(session);
      continue;
    }

    handleSession(session);
    ssh_disconnect(session);
    ssh_free(session);
  }
}

void SshServerTask::sshTaskEntry(void *param)
{
  static_cast<SshServerTask*>(param)->runServer();
  vTaskDelete(NULL);
}

#endif // ENABLE_SSH_CLI
