#include "entropy.hpp"
#include <arch/x86_64/random.hpp>
#include "../sync/wait.hpp"
#include "../tpm/tpm.hpp"

namespace kernel::platform {
abi::syscall::Status random_bytes(uint8_t* out, size_t size, bool nonblock) {
  using abi::syscall::Status;
  if (!size) return Status::Ok;
  bool cpu = arch::x86_64::has_rdrand();
  if (cpu) {
    size_t offset = 0;
    while (offset < size) {
      uint64_t word = 0;
      bool valid = false;
      for (unsigned retry = 0; retry < 10 && !valid; ++retry)
        valid = arch::x86_64::random_word(&word);
      if (!valid) break;
      for (unsigned i = 0; i < 8 && offset < size; ++i, ++offset)
        out[offset] = static_cast<uint8_t>(word >> (i * 8));
    }
    if (offset == size) return Status::Ok;
  }
  if (!tpm::present()) return cpu ? Status::WouldBlock : Status::NotSupported;
  if (nonblock) return Status::WouldBlock;
  sync::MutexGuard guard(tpm::runtime_mutex());
  return tpm::get_random(out, size) ? Status::Ok : Status::IoError;
}
}
