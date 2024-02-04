#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_EMBEDDED_FILES)
#undef ENABLE_DEBUG
#endif

#include "embedded_files.h"
#include "emonesp.h"

bool embedded_get_file(String filename, StaticFile *index, size_t length, StaticFile **file)
{
  DBUGF("Looking for %s", filename.c_str());

  for(int i = 0; i < length; i++)
  {
    if(filename == index[i].filename)
    {
      DBUGF("Found %s %d@%p", index[i].filename, index[i].length, index[i].data);

      if(file) {
        *file = &index[i];
      }
      return true;
    }
  }

  return false;
}