#include "vfs_api.h"

#include <LittleFS.h>

using namespace fs;

class LittleFSImpl : public VFSImpl
{
public:
    LittleFSImpl();
    virtual ~LittleFSImpl() { }
    virtual bool exists(const char* path);
};

LittleFSImpl::LittleFSImpl()
{
}

bool LittleFSImpl::exists(const char* path)
{
    File f = open(path, "r",false);
    return (f == true);
}


LittleFSFS::LittleFSFS() : FS(FSImplPtr(new LittleFSImpl()))
{

}

LittleFSFS::~LittleFSFS()
{

}

bool LittleFSFS::begin(bool formatOnFail, const char * basePath, uint8_t maxOpenFiles, const char * partitionLabel)
{
  return true;
}

void LittleFSFS::end()
{

}

bool LittleFSFS::format()
{
  return true;
}

size_t LittleFSFS::usedBytes()
{
  return 0;
}

LittleFSFS LittleFS;
