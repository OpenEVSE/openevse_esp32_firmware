#ifndef _OPENEVSE_CLI_RUNNINGCONFIG_H
#define _OPENEVSE_CLI_RUNNINGCONFIG_H

#include "cli_types.h"

// Streams a Cisco-IOS-style "show running-config" rendering directly to out,
// line by line, rather than building the whole thing in one String — bounds
// peak heap usage on a target with ~100-140KB free.
void buildRunningConfig(CliOutput &out);

#endif // _OPENEVSE_CLI_RUNNINGCONFIG_H
