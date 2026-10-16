#ifdef OTA_TEST_SINK

#include "ota_test_sink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>

static FILE *image = nullptr;

static std::string sinkPath(const char *name)
{
  const char *dir = getenv("OPENEVSE_OTA_SINK_DIR");
  if(!dir || !*dir) {
    return std::string();
  }
  return std::string(dir) + "/" + name;
}

void ota_test_sink_begin(const char *source, size_t begin_size, size_t expected_size)
{
  if(image) {
    fclose(image);
    image = nullptr;
  }
  std::string path = sinkPath("ota_begin.json");
  if(path.empty()) {
    return;
  }
  FILE *f = fopen(path.c_str(), "w");
  if(f) {
    // source is a filename or URL from the request; keep quotes out of the JSON
    fprintf(f, "{\"source\":\"");
    for(const char *p = source; p && *p; p++) {
      if(*p != '"' && *p != '\\') fputc(*p, f);
    }
    fprintf(f, "\",\"begin_size\":%zu,\"expected_size\":%zu}\n", begin_size, expected_size);
    fclose(f);
  }
  path = sinkPath("ota_image.bin");
  image = fopen(path.c_str(), "wb");
}

void ota_test_sink_write(const uint8_t *data, size_t len)
{
  if(image && len > 0) {
    fwrite(data, 1, len, image);
    fflush(image);
  }
}

void ota_test_sink_end(bool ok, size_t written)
{
  if(image) {
    fclose(image);
    image = nullptr;
  }
  std::string path = sinkPath("ota_end.json");
  if(path.empty()) {
    return;
  }
  FILE *f = fopen(path.c_str(), "w");
  if(f) {
    fprintf(f, "{\"ok\":%s,\"written\":%zu}\n", ok ? "true" : "false", written);
    fclose(f);
  }
}

#endif // OTA_TEST_SINK
