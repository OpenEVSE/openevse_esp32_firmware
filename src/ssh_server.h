#ifndef _OPENEVSE_SSH_SERVER_H
#define _OPENEVSE_SSH_SERVER_H

#ifdef ENABLE_SSH_CLI

#include <Arduino.h>
#include <MicroTasks.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <libssh/libssh.h>
#include "cli/cli_types.h"
#include "evse_man.h"

// Bridges the libssh blocking C API onto this firmware's MicroTasks model.
//
// libssh's API (ssh_bind_accept, ssh_message_get, ssh_channel_read) is
// synchronous/blocking — it cannot run inside MicroTasks::loop(), which is
// cooperative and must return promptly or it stalls every other subsystem
// (MQTT, OCPP, the web server, ...). So the entire accept/auth/session I/O
// loop runs on its own dedicated FreeRTOS task with its own stack; the
// MicroTasks::Task side only drains a request queue, and is what actually
// *executes* resolved CLI commands (see CliExecutor below), so that
// config_set()/EvseManager calls always happen on the same main thread every
// other subsystem already assumes, never concurrently from the SSH task.
//
// Single-session by construction: the dedicated task only calls
// ssh_bind_accept() again after the previous session's handleSession() has
// returned, so there is never more than one session in flight.
class SshServerTask : public MicroTasks::Task, public CliExecutor {
  public:
    SshServerTask(EvseManager &evse);

    void begin();
    void notifyConfigChanged();

    // CliExecutor — called from the dedicated SSH task; marshals onto the
    // main MicroTasks thread via _cmdQueue and blocks until done.
    void execute(CliHandler handler, int argc, const char *argv[], CliOutput &out) override;

  protected:
    void setup() override;
    unsigned long loop(MicroTasks::WakeReason reason) override;

  private:
    EvseManager &_evse;
    TaskHandle_t _sshTaskHandle;
    QueueHandle_t _cmdQueue;
    bool _started;
    // Loaded/generated in setup() — on the main MicroTasks thread, since
    // doing so touches config_set()/config_commit() (LittleFS-backed config),
    // which must never be called from the dedicated SSH FreeRTOS task. The
    // spawned task only ever reads this pointer afterwards.
    ssh_key _hostKey;

    struct CliCmdRequest {
      CliHandler handler;
      int argc;
      const char *argv[CLI_MAX_TOKENS + 1];
      CliOutput *out;
      SemaphoreHandle_t done;
    };

    static void sshTaskEntry(void *param);
    void runServer();
    void handleSession(ssh_session session);
    bool authenticate(ssh_session session);
    ssh_key loadOrCreateHostKey();
};

extern SshServerTask sshServer;

#endif // ENABLE_SSH_CLI

#endif // _OPENEVSE_SSH_SERVER_H
