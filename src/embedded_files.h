#ifndef EMBEDDED_FILES_H
#define EMBEDDED_FILES_H

#include <Arduino.h>

#define ARRAY_LENGTH(x) (sizeof(x)/sizeof((x)[0]))

#define IS_ALIGNED(x)   (0 == ((uint32_t)(x) & 0x3))

struct StaticFile
{
  const char *filename;
  const char *data;
  size_t length;
  const char *type;
  const char *etag;
  bool compressed;
};

bool embedded_get_file(String filename, StaticFile *index, size_t length, StaticFile **file);

#endif // EMBEDDED_FILES_H
