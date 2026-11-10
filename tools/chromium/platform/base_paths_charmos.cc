#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include <stdlib.h>
#include <utility>

namespace base {
bool PathProviderPosix(int key, FilePath* result) {
  if (!result) return false;
  // The native startup ABI does not expose an executable path and there is no
  // /proc. Pin the two paths the browser service is installed at: DIR_EXE is
  // derived from FILE_EXE, while DIR_ASSETS is the directory of FILE_MODULE.
  if (key == FILE_EXE) {
    *result = FilePath("/bin/content_shell");
    return true;
  }
  if (key == FILE_MODULE) {
    *result = FilePath("/data/content_shell/content_shell");
    return true;
  }
  FilePath path;
  if (key == DIR_CACHE) {
    const char* configured = getenv("XDG_CACHE_HOME");
    if (configured && *configured) path = FilePath(configured);
    else {
      const char* home = getenv("HOME");
      if (!home || !*home) return false;
      FilePath parent(home);
      if (!parent.IsAbsolute()) return false;
      path = parent.Append(".cache");
    }
  } else if (key == DIR_USER_DESKTOP) {
    const char* configured = getenv("XDG_DESKTOP_DIR");
    if (!configured || !*configured) return false;
    path = FilePath(configured);
  } else {
    // The current startup ABI has no authoritative executable/module path.
    // A launcher can override a known path explicitly. Never infer one from
    // argv[0], a host /proc filesystem or a guessed installation directory.
    return false;
  }
  if (!path.IsAbsolute() || !DirectoryExists(path)) return false;
  *result = std::move(path);
  return true;
}
}  // namespace base
