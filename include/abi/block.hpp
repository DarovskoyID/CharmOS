#pragma once
// Протокол «ядро — блочный драйвер», ABI v1.3. Нормативный документ —
// docs/abi/block.md.

#include <stddef.h>
#include <stdint.h>

namespace abi::block {

inline constexpr char kHelloMagic[4] = {'b', 'l', 'k', '1'};
inline constexpr uint32_t kHelloReadOnly = 1u << 0;
inline constexpr uint32_t kHelloFlush = 1u << 1;

// §5.1
struct BlockHello {
  char magic[4];
  uint32_t block_size;
  uint64_t block_count;
  uint32_t max_blocks;
  uint32_t flags;
  uint64_t reserved;
};
static_assert(sizeof(BlockHello) == 32);
static_assert(offsetof(BlockHello, block_count) == 8);
static_assert(offsetof(BlockHello, max_blocks) == 16);
static_assert(offsetof(BlockHello, flags) == 20);

// §5.2
struct RingHeader {
  uint32_t sq_head;
  uint32_t sq_tail;
  uint32_t cq_head;
  uint32_t cq_tail;
  uint32_t driver_wants_kick;
  uint32_t kernel_wants_kick;
  uint8_t reserved[40];
};
static_assert(sizeof(RingHeader) == 64);
static_assert(offsetof(RingHeader, driver_wants_kick) == 16);

inline constexpr uint32_t kOpRead = 1;
inline constexpr uint32_t kOpWrite = 2;
inline constexpr uint32_t kOpFlush = 3;
inline constexpr uint32_t kMaxSegments = 6;

struct Segment {
  uint64_t iova;
  uint32_t bytes;
  uint32_t reserved;
};
static_assert(sizeof(Segment) == 16);

// §5.3
struct Request {
  uint64_t id;
  uint32_t op;
  uint32_t segment_count;
  uint64_t lba;
  uint32_t blocks;
  uint32_t reserved;
  Segment segments[kMaxSegments];
};
static_assert(sizeof(Request) == 128);
static_assert(offsetof(Request, lba) == 16);
static_assert(offsetof(Request, blocks) == 24);
static_assert(offsetof(Request, segments) == 32);

// §5.4
struct Completion {
  uint64_t id;
  int32_t status;
  uint32_t reserved;
};
static_assert(sizeof(Completion) == 16);

// §3
inline constexpr uint32_t kRingEntries = 64;
inline constexpr uint64_t kRingPages = 4;
inline constexpr uint64_t kSqOffset = 4096;
inline constexpr uint64_t kCqOffset = 12288;

}  // namespace abi::block
