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
  // Content-Encoding the bytes were stored with, or NULL to serve them as-is.
  // A string rather than a bool so a future encoding (e.g. brotli) is just a
  // different literal here, with no plumbing change.
  const char *encoding;
};

bool embedded_get_file(String filename, StaticFile *index, size_t length, StaticFile **file);

#endif // EMBEDDED_FILES_H
