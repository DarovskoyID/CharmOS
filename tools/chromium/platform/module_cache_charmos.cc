#include "base/profiler/module_cache.h"
#include "base/command_line.h"
#include <memory>

extern "C" const char __charm_text_start[],__charm_text_end[];

namespace base {
namespace {
class NativeImage final : public ModuleCache::Module {
 public:
  uintptr_t GetBaseAddress() const override { return reinterpret_cast<uintptr_t>(__charm_text_start); }
  std::string GetId() const override { return {}; } // No build-id note in this static profile.
  FilePath GetDebugBasename() const override {
    return CommandLine::InitializedForCurrentProcess() ?
        CommandLine::ForCurrentProcess()->GetProgram().BaseName() : FilePath();
  }
  size_t GetSize() const override {
    return reinterpret_cast<uintptr_t>(__charm_text_end)-reinterpret_cast<uintptr_t>(__charm_text_start);
  }
  bool IsNative() const override { return true; }
};
}
std::unique_ptr<const ModuleCache::Module> ModuleCache::CreateModuleForAddress(uintptr_t address) {
  auto begin=reinterpret_cast<uintptr_t>(__charm_text_start);
  auto end=reinterpret_cast<uintptr_t>(__charm_text_end);
  if(address<begin || address>=end) return nullptr;
  return std::make_unique<NativeImage>();
}
} // namespace base
