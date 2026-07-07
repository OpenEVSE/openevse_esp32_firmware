#ifndef _OPENEVSE_CLI_TYPES_H
#define _OPENEVSE_CLI_TYPES_H

#include <Arduino.h>

#define CLI_LINE_MAX     128
#define CLI_MAX_TOKENS   12

// A node is visible/matchable only while the engine is in one of these modes.
// CLI_MODE_UNPRIV is the mode every session starts in (user EXEC, "host>") —
// only "show version/status/faults" and "enable" are reachable. "enable"
// promotes to CLI_MODE_EXEC (privileged EXEC, "host#"), which exposes
// everything else, including "configure terminal".
enum CliMode : uint8_t {
  CLI_MODE_UNPRIV = 1,
  CLI_MODE_EXEC   = 2,
  CLI_MODE_CONFIG = 4,
  CLI_MODE_BOTH   = CLI_MODE_EXEC | CLI_MODE_CONFIG
};

// Describes the free-form argument(s), if any, expected after a terminator node.
enum CliArgKind : uint8_t {
  CLI_ARG_NONE,    // command takes no further arguments
  CLI_ARG_WORD,    // a single free-form word (e.g. enable|disable, a hostname)
  CLI_ARG_NUMBER   // a single numeric argument (e.g. an amp/voltage value)
};

// Sink commands write their output to. Implemented by the transport (SSH channel)
// so cli_engine/cli_commands stay transport-agnostic.
class CliOutput {
  public:
    virtual void print(const char *s) = 0;
    void println(const char *s) {
      print(s);
      print("\r\n");
    }
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

typedef void (*CliHandler)(CliOutput &out, int argc, const char *argv[]);

// Flash/.rodata-resident command tree node (no heap copy, no PROGMEM macros needed on ESP32).
struct CliNode {
  const char *name;
  const char *help;
  const CliNode *children;
  uint8_t childCount;
  CliHandler handler;   // non-null = this node is a valid command terminator
  uint8_t mode;         // CliMode
  uint8_t argKind;      // CliArgKind, only meaningful when handler != nullptr
};

// Implemented by the SSH transport layer so that command *execution* (which touches
// shared firmware state via config_set()/EvseManager) always runs on the main
// MicroTasks loop thread, never on the dedicated libssh I/O task. Tree walking,
// tab-completion and '?' help do not go through this — they only read the
// const command tree and per-session state, so they're safe to run directly on
// the SSH I/O task.
class CliExecutor {
  public:
    virtual void execute(CliHandler handler, int argc, const char *argv[], CliOutput &out) = 0;
};

#endif // _OPENEVSE_CLI_TYPES_H
