#include "cli_types.h"
#include <stdarg.h>

void CliOutput::printf(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  print(buf);
}
