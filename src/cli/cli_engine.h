#ifndef _OPENEVSE_CLI_ENGINE_H
#define _OPENEVSE_CLI_ENGINE_H

#include "cli_types.h"

#define CLI_HISTORY_SIZE 10

// Transport-agnostic line editor + command-tree walker for one SSH session.
// Knows nothing about SSH or libssh — feedByte() is driven by whatever
// transport owns the byte stream (src/ssh_server.cpp).
//
// v1 scope: flat append/backspace-at-end line buffer — no left/right cursor
// movement within a line (would need full ANSI cursor-addressing, not just
// the up/down-arrow history recall below, which only ever replaces the
// whole line rather than repositioning a cursor inside it).
class CliEngine {
  public:
    CliEngine(CliExecutor &executor, const char *hostname);

    // Feed one input byte; writes any echo/output directly via the output
    // sink. Returns true once the session should close (user typed "exit"
    // at the EXEC root).
    bool feedByte(uint8_t b, CliOutput &out);

    void printPrompt(CliOutput &out);

  private:
    enum EscState : uint8_t { ESC_NONE, ESC_GOT_ESC, ESC_GOT_CSI };

    CliExecutor &_executor;
    const char *_hostname;
    CliMode _mode;
    char _lineBuf[CLI_LINE_MAX];
    uint8_t _lineLen;
    bool _shouldClose;
    EscState _escState;

    // Ring buffer of previously entered lines, newest written at
    // _historyNext-1. _historyPos tracks how far back Up-arrow browsing has
    // gone (-1 = not browsing / back at a blank line).
    char _history[CLI_HISTORY_SIZE][CLI_LINE_MAX];
    uint8_t _historyCount;
    uint8_t _historyNext;
    int8_t _historyPos;

    void dispatchLine(CliOutput &out);
    void handleTab(CliOutput &out);
    void handleHelp(CliOutput &out);
    void resetLine();
    void pushHistory();
    void historyUp(CliOutput &out);
    void historyDown(CliOutput &out);
    void replaceLine(const char *newLine, CliOutput &out);

    // Tokenizes a copy of the line; returns token count. Tokens point into
    // `scratch` (caller-owned buffer of at least CLI_LINE_MAX bytes).
    static uint8_t tokenize(const char *line, char *scratch, const char *tokens[], uint8_t maxTokens);

    const CliNode *rootForMode(uint8_t &count) const;
};

#endif // _OPENEVSE_CLI_ENGINE_H
