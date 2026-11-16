#pragma once
#include <abi/block.hpp>
#include <abi/syscall.hpp>

namespace virtio_blk {
inline constexpr uint32_t kMaxBlocks = 2048;  // 1 MiB, block ABI v1.4.

inline abi::syscall::Status validate_request(const abi::block::Request& r,
    uint64_t capacity, bool read_only, bool flush_supported) {
  using abi::syscall::Status;
  if (r.reserved != 0) return Status::InvalidArgs;
  if (r.op == abi::block::kOpFlush) {
    if (r.segment_count != 0 || r.blocks != 0) return Status::InvalidArgs;
    return flush_supported ? Status::Ok : Status::NotSupported;
  }
  if ((r.op != abi::block::kOpRead && r.op != abi::block::kOpWrite) ||
      r.segment_count == 0 || r.segment_count > abi::block::kMaxSegments ||
      r.blocks == 0 || r.blocks > kMaxBlocks) return Status::InvalidArgs;
  if (r.lba > capacity || r.blocks > capacity - r.lba) return Status::OutOfRange;
  if (r.op == abi::block::kOpWrite && read_only) return Status::AccessDenied;
  uint64_t bytes = 0;
  for (uint32_t i = 0; i < r.segment_count; ++i) {
    const auto& s = r.segments[i];
    if (s.reserved != 0 || s.bytes == 0 || (s.bytes & 511) != 0 ||
        (s.iova & 511) != 0 || s.iova > UINT64_MAX - s.bytes)
      return Status::InvalidArgs;
    bytes += s.bytes;
  }
  return bytes == uint64_t{r.blocks} * 512 ? Status::Ok : Status::InvalidArgs;
}
}  // namespace virtio_blk
