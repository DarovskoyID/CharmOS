#include "boot_info.hpp"
#include "test.hpp"

using abi::boot::BootInfo;
using kernel::check_boot_info;

namespace {

BootInfo valid() {
  BootInfo b = {};
  b.magic = abi::boot::kMagic;
  b.version_major = abi::boot::kVersionMajor;
  b.version_minor = abi::boot::kVersionMinor;
  b.size = sizeof(BootInfo);
  b.hhdm_base = abi::boot::kHhdmBase;
  b.memory_map = 0x1000;
  b.memory_map_count = 1;
  return b;
}

}  // namespace

TEST(boot_info_valid) { CHECK(check_boot_info(valid()) == nullptr); }

TEST(boot_info_newer_minor_accepted) {
  BootInfo b = valid();
  b.version_minor = 7;
  b.size = sizeof(BootInfo) + 64;
  CHECK(check_boot_info(b) == nullptr);
}

TEST(boot_info_rejects) {
  { BootInfo b = valid(); b.magic = 0; CHECK(check_boot_info(b) != nullptr); }
  { BootInfo b = valid(); b.version_major = 2; CHECK(check_boot_info(b) != nullptr); }
  { BootInfo b = valid(); b.size = 8; CHECK(check_boot_info(b) != nullptr); }
  { BootInfo b = valid(); b.hhdm_base = 0; CHECK(check_boot_info(b) != nullptr); }
  { BootInfo b = valid(); b.memory_map_count = 0; CHECK(check_boot_info(b) != nullptr); }
}
