#include "boot_info.hpp"

namespace kernel {

const char* check_boot_info(const abi::boot::BootInfo& info) {
  if (info.magic != abi::boot::kMagic) return "bad magic";
  if (info.version_major != abi::boot::kVersionMajor) return "unsupported major version";
  // Ядро читает поля только в пределах size; меньше v1.0 быть не может.
  if (info.size < abi::boot::kV1BaseSize) return "BootInfo too small";
  if (info.version_minor>=1 && (info.flags&abi::boot::kFlagRealtime) &&
      info.size<sizeof(abi::boot::BootInfo)) return "BootInfo time extension too small";
  if (info.version_minor >= 1 && (info.flags & abi::boot::kFlagRtcAssumedUtc) &&
      !(info.flags & abi::boot::kFlagRealtime)) return "BootInfo UTC assumption without time";
  if (info.hhdm_base != abi::boot::kHhdmBase) return "unexpected hhdm_base";
  if (info.memory_map == 0 || info.memory_map_count == 0) return "empty memory map";
  return nullptr;
}

}  // namespace kernel
