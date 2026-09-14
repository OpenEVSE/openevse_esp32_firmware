#include <cstddef>
#include <cstdlib>
#include <dlfcn.h>
#include <new>
#include <unistd.h>

void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept
{
  using Allocator = void *(*)(std::size_t, const std::nothrow_t &) noexcept;
  static Allocator allocate = reinterpret_cast<Allocator>(
      dlsym(RTLD_NEXT, "_ZnamRKSt9nothrow_t"));

  const char *marker = std::getenv("OPENEVSE_FAIL_NOTHROW_NEW_ARRAY_MARKER");
  if(nullptr != marker && '\0' != marker[0] && 0 == access(marker, F_OK)) {
    unlink(marker);
    return nullptr;
  }

  return allocate(size, tag);
}
