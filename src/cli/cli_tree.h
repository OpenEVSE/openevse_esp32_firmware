#ifndef _OPENEVSE_CLI_TREE_H
#define _OPENEVSE_CLI_TREE_H

#include "cli_types.h"

// Root of the unprivileged EXEC-mode command tree (the `hostname>` prompt
// every session starts in — only show version/status/faults and "enable").
extern const CliNode cliUnprivRoot[];
extern const uint8_t cliUnprivRootCount;

// Root of the privileged EXEC-mode command tree (the `hostname#` prompt,
// entered via "enable").
extern const CliNode cliExecRoot[];
extern const uint8_t cliExecRootCount;

// Root of the CONFIG-mode command tree (the `hostname(config)#` prompt,
// entered via "configure terminal").
extern const CliNode cliConfigRoot[];
extern const uint8_t cliConfigRootCount;

#endif // _OPENEVSE_CLI_TREE_H
