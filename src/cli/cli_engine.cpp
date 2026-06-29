#include "cli_engine.h"
#include "cli_tree.h"
#include "cli_commands.h"
#include <string.h>
#include <stdlib.h>

CliEngine::CliEngine(CliExecutor &executor, const char *hostname) :
  _executor(executor),
  _hostname(hostname),
  _mode(CLI_MODE_UNPRIV),
  _lineLen(0),
  _shouldClose(false),
  _escState(ESC_NONE),
  _historyCount(0),
  _historyNext(0),
  _historyPos(-1)
{
  _lineBuf[0] = 0;
}

const CliNode *CliEngine::rootForMode(uint8_t &count) const
{
  if(CLI_MODE_CONFIG == _mode) {
    count = cliConfigRootCount;
    return cliConfigRoot;
  }
  if(CLI_MODE_EXEC == _mode) {
    count = cliExecRootCount;
    return cliExecRoot;
  }
  count = cliUnprivRootCount;
  return cliUnprivRoot;
}

void CliEngine::printPrompt(CliOutput &out)
{
  if(CLI_MODE_CONFIG == _mode) {
    out.printf("%s(config)#", _hostname);
  } else if(CLI_MODE_EXEC == _mode) {
    out.printf("%s#", _hostname);
  } else {
    out.printf("%s>", _hostname);
  }
}

void CliEngine::resetLine()
{
  _lineLen = 0;
  _lineBuf[0] = 0;
  _historyPos = -1;
}

uint8_t CliEngine::tokenize(const char *line, char *scratch, const char *tokens[], uint8_t maxTokens)
{
  strncpy(scratch, line, CLI_LINE_MAX - 1);
  scratch[CLI_LINE_MAX - 1] = 0;

  uint8_t n = 0;
  char *p = scratch;
  while(*p && n < maxTokens) {
    while(*p == ' ') p++;
    if(!*p) break;
    tokens[n++] = p;
    while(*p && *p != ' ') p++;
    if(*p) { *p = 0; p++; }
  }
  return n;
}

// Commands that work when typed in full but are left out of '?' help and
// Tab-completion suggestions — e.g. "do" (config-mode escape to run an EXEC
// show/write command), which is real and usable but not meant to clutter the
// menus. Checked by name rather than a CliNode struct field so existing node
// tables (most of which target -std=gnu++11 here, where a struct with a
// non-static member initializer stops being an aggregate and breaks their
// brace-init) don't all need a trailing field added.
static const char *hiddenNodeNames[] = { "do" };
static bool isHiddenNode(const CliNode &node)
{
  for(size_t i = 0; i < sizeof(hiddenNodeNames) / sizeof(hiddenNodeNames[0]); i++) {
    if(0 == strcmp(node.name, hiddenNodeNames[i])) return true;
  }
  return false;
}

// Finds children whose name starts with `token`. Returns the number of
// matches (0, 1, or >1) and fills `match` with the first one found — callers
// only use `match` when exactly 1 is returned, matching Cisco IOS's
// unambiguous-abbreviation rule (no separate per-command "minimum length").
// `excludeHidden` is set only when matching the partial token a user is
// actively completing (Tab) or asking about ('?') — a hidden node already
// fully typed as a *fixed* prior token must still resolve normally, so the
// fixed-prefix resolution walks in handleTab()/handleHelp() pass false.
static uint8_t matchToken(const CliNode *children, uint8_t count, const char *token, const CliNode **match, bool excludeHidden = false)
{
  uint8_t n = 0;
  size_t len = strlen(token);
  for(uint8_t i = 0; i < count; i++) {
    if(excludeHidden && isHiddenNode(children[i])) continue;
    if(0 == strncmp(children[i].name, token, len)) {
      *match = &children[i];
      n++;
    }
  }
  return n;
}

void CliEngine::dispatchLine(CliOutput &out)
{
  out.print("\r\n");

  char scratch[CLI_LINE_MAX];
  const char *rawTokens[CLI_MAX_TOKENS];
  uint8_t ntok = tokenize(_lineBuf, scratch, rawTokens, CLI_MAX_TOKENS);

  if(ntok > 0) {
    pushHistory();
  }

  if(0 == ntok) {
    return;
  }

  uint8_t count;
  const CliNode *children = rootForMode(count);
  const CliNode *resolved = nullptr;
  uint8_t i = 0;

  while(i < ntok) {
    const CliNode *match = nullptr;
    uint8_t nm = matchToken(children, count, rawTokens[i], &match);
    if(0 == nm) {
      if(resolved && resolved->handler) break; // remaining tokens are free-form args
      out.printf("%% Unrecognized command: \"%s\"\r\n", rawTokens[i]);
      return;
    }
    if(nm > 1) {
      out.printf("%% Ambiguous command: \"%s\"\r\n", rawTokens[i]);
      return;
    }
    resolved = match;
    children = resolved->children;
    count = resolved->childCount;
    i++;
    if(!children || 0 == count) break; // nothing to descend into; rest is free args
  }

  if(!resolved || !resolved->handler) {
    out.println("% Incomplete command");
    return;
  }

  if(CLI_ARG_NONE == resolved->argKind && i < ntok) {
    out.println("% Too many parameters");
    return;
  }

  if(cmd_enter_exec_mode == resolved->handler) {
    _mode = CLI_MODE_EXEC;
    return;
  }
  if(cmd_enter_config_mode == resolved->handler) {
    _mode = CLI_MODE_CONFIG;
    return;
  }
  if(cmd_exit_mode == resolved->handler) {
    _mode = CLI_MODE_EXEC;
    return;
  }
  if(cmd_exit_to_unpriv == resolved->handler) {
    _mode = CLI_MODE_UNPRIV;
    return;
  }
  if(cmd_exit_session == resolved->handler) {
    _shouldClose = true;
    return;
  }

  const char *argv[CLI_MAX_TOKENS + 1];
  argv[0] = resolved->name;
  int argc = 1;
  for(uint8_t j = i; j < ntok; j++) {
    argv[argc++] = rawTokens[j];
  }

  _executor.execute(resolved->handler, argc, argv, out);
}

void CliEngine::handleTab(CliOutput &out)
{
  char scratch[CLI_LINE_MAX];
  const char *rawTokens[CLI_MAX_TOKENS];
  uint8_t ntok = tokenize(_lineBuf, scratch, rawTokens, CLI_MAX_TOKENS);

  bool trailingSpace = (0 == _lineLen) || (' ' == _lineBuf[_lineLen - 1]);
  uint8_t fixedTokens = trailingSpace ? ntok : (ntok > 0 ? ntok - 1 : 0);
  const char *partial = trailingSpace ? "" : rawTokens[ntok - 1];

  uint8_t count;
  const CliNode *children = rootForMode(count);
  for(uint8_t i = 0; i < fixedTokens; i++) {
    const CliNode *match = nullptr;
    if(1 != matchToken(children, count, rawTokens[i], &match) || !match->children) {
      return; // can't resolve the fixed prefix unambiguously; nothing to complete
    }
    children = match->children;
    count = match->childCount;
  }

  const CliNode *match = nullptr;
  if(1 != matchToken(children, count, partial, &match, true)) {
    return; // zero or ambiguous matches (hidden nodes excluded): no-op, matching a single real Tab press in IOS
  }

  const char *toAppend = match->name + strlen(partial);
  while(*toAppend && _lineLen < CLI_LINE_MAX - 2) {
    _lineBuf[_lineLen++] = *toAppend++;
  }
  _lineBuf[_lineLen++] = ' ';
  _lineBuf[_lineLen] = 0;
  out.print(match->name + strlen(partial));
  out.print(" ");
  _historyPos = -1;
}

void CliEngine::handleHelp(CliOutput &out)
{
  char scratch[CLI_LINE_MAX];
  const char *rawTokens[CLI_MAX_TOKENS];
  uint8_t ntok = tokenize(_lineBuf, scratch, rawTokens, CLI_MAX_TOKENS);

  bool trailingSpace = (0 == _lineLen) || (' ' == _lineBuf[_lineLen - 1]);
  uint8_t fixedTokens = trailingSpace ? ntok : (ntok > 0 ? ntok - 1 : 0);
  const char *partial = trailingSpace ? "" : rawTokens[ntok - 1];

  // Walk the fixed (already-typed) tokens. Unlike dispatchLine(), this stops
  // as soon as it lands on a terminator leaf (no children) — anything left
  // over in `extra` is the argument value the user has already supplied (or
  // is mid-typing, if there's no trailing space), not a child-name token.
  uint8_t count;
  const CliNode *children = rootForMode(count);
  const CliNode *resolved = nullptr;
  bool resolvable = true;
  uint8_t i = 0;
  while(i < fixedTokens) {
    const CliNode *match = nullptr;
    if(1 != matchToken(children, count, rawTokens[i], &match)) {
      resolvable = false;
      break;
    }
    resolved = match;
    i++;
    if(!match->children) break; // terminator leaf; nothing more to descend into
    children = match->children;
    count = match->childCount;
  }
  uint8_t extra = fixedTokens - i;

  out.print("\r\n");

  if(!resolvable) {
    out.println("% Unrecognized command");
  } else if(extra > 1 || (1 == extra && resolved && CLI_ARG_NONE == resolved->argKind)) {
    out.println("% Too many parameters");
  } else if(1 == extra) {
    // An argument value has already been supplied — command is complete.
    out.println("  <cr>");
  } else if(resolved && !resolved->children) {
    // Sitting right after a terminator leaf, no value typed yet. `children`/
    // `count` here belong to its *parent's* sibling array, not to this node,
    // so don't list them — show what kind of value is expected instead.
    if(CLI_ARG_NONE == resolved->argKind) {
      out.println("  <cr>");
    } else {
      const char *placeholder = (CLI_ARG_NUMBER == resolved->argKind) ? "<number>" : "WORD";
      out.printf("  %-20s %s\r\n", placeholder, resolved->help);
    }
  } else {
    // At the root (resolved == nullptr) or sitting on a parent node — list
    // its children matching `partial`, plus "<cr>" if the node itself is
    // ALSO a valid terminator (e.g. "mqtt", which both stands alone and has
    // a "topics" child).
    size_t len = strlen(partial);
    for(uint8_t j = 0; j < count; j++) {
      if(isHiddenNode(children[j])) continue;
      if(0 == strncmp(children[j].name, partial, len)) {
        out.printf("  %-20s %s\r\n", children[j].name, children[j].help);
      }
    }
    if(resolved && resolved->handler) {
      out.println("  <cr>");
    }
  }

  printPrompt(out);
  out.print(_lineBuf);
}

void CliEngine::pushHistory()
{
  if(_historyCount > 0) {
    uint8_t lastIdx = (_historyNext + CLI_HISTORY_SIZE - 1) % CLI_HISTORY_SIZE;
    if(0 == strcmp(_history[lastIdx], _lineBuf)) {
      return; // don't duplicate consecutive identical entries, same as IOS
    }
  }
  strncpy(_history[_historyNext], _lineBuf, CLI_LINE_MAX - 1);
  _history[_historyNext][CLI_LINE_MAX - 1] = 0;
  _historyNext = (_historyNext + 1) % CLI_HISTORY_SIZE;
  if(_historyCount < CLI_HISTORY_SIZE) _historyCount++;
}

void CliEngine::replaceLine(const char *newLine, CliOutput &out)
{
  for(uint8_t i = 0; i < _lineLen; i++) {
    out.print("\b \b");
  }
  strncpy(_lineBuf, newLine, CLI_LINE_MAX - 1);
  _lineBuf[CLI_LINE_MAX - 1] = 0;
  _lineLen = strlen(_lineBuf);
  out.print(_lineBuf);
}

void CliEngine::historyUp(CliOutput &out)
{
  if(0 == _historyCount) return;
  int8_t maxPos = _historyCount - 1;
  if(_historyPos < maxPos) _historyPos++;
  uint8_t idx = (_historyNext + CLI_HISTORY_SIZE - 1 - _historyPos) % CLI_HISTORY_SIZE;
  replaceLine(_history[idx], out);
}

void CliEngine::historyDown(CliOutput &out)
{
  if(_historyPos < 0) return;
  _historyPos--;
  if(_historyPos < 0) {
    replaceLine("", out);
    return;
  }
  uint8_t idx = (_historyNext + CLI_HISTORY_SIZE - 1 - _historyPos) % CLI_HISTORY_SIZE;
  replaceLine(_history[idx], out);
}

bool CliEngine::feedByte(uint8_t b, CliOutput &out)
{
  if(ESC_GOT_ESC == _escState) {
    _escState = ('[' == b) ? ESC_GOT_CSI : ESC_NONE;
    return false;
  }
  if(ESC_GOT_CSI == _escState) {
    _escState = ESC_NONE;
    if('A' == b) historyUp(out);
    else if('B' == b) historyDown(out);
    // 'C'/'D' (left/right) intentionally ignored — no in-line cursor addressing in v1
    return false;
  }

  if(0x1B == b) {
    _escState = ESC_GOT_ESC;
    return false;
  }

  if('\r' == b || '\n' == b) {
    dispatchLine(out);
    out.print("\r\n"); // blank line between a command's output and the next prompt
    resetLine();
    printPrompt(out);
    return _shouldClose;
  }

  if(0x08 == b || 0x7F == b) { // backspace / DEL
    if(_lineLen > 0) {
      _lineLen--;
      _lineBuf[_lineLen] = 0;
      out.print("\b \b");
    }
    _historyPos = -1;
    return false;
  }

  if('\t' == b) {
    handleTab(out);
    return false;
  }

  if('?' == b) {
    handleHelp(out);
    return false;
  }

  if(0x03 == b) { // Ctrl-C
    out.print("\r\n");
    resetLine();
    printPrompt(out);
    return false;
  }

  if(b >= 0x20 && b < 0x7F && _lineLen < CLI_LINE_MAX - 1) {
    _lineBuf[_lineLen++] = (char)b;
    _lineBuf[_lineLen] = 0;
    char ch[2] = { (char)b, 0 };
    out.print(ch);
    _historyPos = -1;
  }

  return false;
}
