#include "build/charmos/filesystem_charmos.h"

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace std::filesystem {
namespace {

void SetError(std::error_code& error, int value) {
  error.assign(value, std::generic_category());
}

}  // namespace

bool create_directories(const path& directory, std::error_code& error) {
  error.clear();
  const std::string native = directory.string();
  if (native.empty()) {
    SetError(error, ENOENT);
    return false;
  }
  for (size_t i = 1; i <= native.size(); ++i) {
    if (i != native.size() && native[i] != '/') {
      continue;
    }
    const std::string part = native.substr(0, i);
    if (part.empty() || part == "/") {
      continue;
    }
    if (::mkdir(part.c_str(), 0777) == 0 || errno == EEXIST) {
      continue;
    }
    SetError(error, errno);
    return false;
  }
  return true;
}

bool create_directories(const path& directory) {
  std::error_code error;
  return create_directories(directory, error);
}

bool remove(const path& file, std::error_code& error) {
  error.clear();
  if (::remove(file.string().c_str()) == 0) {
    return true;
  }
  if (errno == ENOENT) {
    return false;
  }
  SetError(error, errno);
  return false;
}

bool remove(const path& file) {
  std::error_code error;
  return remove(file, error);
}

}  // namespace std::filesystem
