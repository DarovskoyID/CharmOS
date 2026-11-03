#include "base/native_library.h"
#include "base/check.h"
#include <errno.h>

namespace base {
std::string NativeLibraryLoadError::ToString() const { return message; }
NativeLibrary LoadNativeLibrary(const FilePath&,NativeLibraryLoadError* error) {
  if(error) error->message="CharmOS static executables do not support dynamic library loading";
  errno=ENOTSUP;
  return nullptr;
}
NativeLibrary LoadNativeLibraryWithOptions(const FilePath& path,const NativeLibraryOptions&,
                                           NativeLibraryLoadError* error) {
  return LoadNativeLibrary(path,error);
}
void UnloadNativeLibrary(NativeLibrary library) {
  // No successful load can produce a valid module handle in this profile.
  CHECK(!library);
}
void* GetFunctionPointerFromNativeLibrary(NativeLibrary,const char*) {
  errno=ENOTSUP;
  return nullptr;
}
std::string GetNativeLibraryName(std::string_view name) {
  // GN's conventional shared-library filename; loading remains unsupported.
  return "lib"+std::string(name)+".so";
}
std::string GetLoadableModuleName(std::string_view name) { return GetNativeLibraryName(name); }
}  // namespace base
