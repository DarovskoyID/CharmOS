#pragma once
// Протокол «ядро — сетевой драйвер», ABI v1.4. Нормативный документ —
// docs/abi/net.md.

#include <stddef.h>
#include <stdint.h>

namespace abi::net {

inline constexpr char kHelloMagic[4] = {'n', 'e', 't', '1'};

// §5.1
struct NetHello {
  char magic[4];
  uint8_t mac[6];
  uint16_t mtu;
  uint32_t buffer_size;
  uint32_t buffer_count;
  uint32_t flags;
  uint64_t reserved;
};
static_assert(sizeof(NetHello) == 32);
static_assert(offsetof(NetHello, mtu) == 10);
static_assert(offsetof(NetHello, buffer_size) == 12);
static_assert(offsetof(NetHello, buffer_count) == 16);

// §5.2
struct NetRingHeader {
  uint32_t tx_head, tx_tail;
  uint32_t rx_free_head, rx_free_tail;
  uint32_t rx_done_head, rx_done_tail;
  uint32_t tx_done_head, tx_done_tail;
  uint32_t driver_wants_kick, kernel_wants_kick;
  uint32_t link_up;
  uint8_t reserved[212];
};
static_assert(sizeof(NetRingHeader) == 256);
static_assert(offsetof(NetRingHeader, driver_wants_kick) == 32);
static_assert(offsetof(NetRingHeader, link_up) == 40);

// §5.3
struct Slot {
  uint32_t buffer;
  uint32_t length;
  int32_t status;
  uint32_t reserved;
};
static_assert(sizeof(Slot) == 16);

// §3
inline constexpr uint32_t kRingEntries = 64;
inline constexpr uint64_t kTxOffset = 256;
inline constexpr uint64_t kRxFreeOffset = 1280;
inline constexpr uint64_t kRxDoneOffset = 2304;
inline constexpr uint64_t kTxDoneOffset = 3328;
inline constexpr uint64_t kRingBytes = 8192;  // 2 страницы (net.md §3, §7)
static_assert(kTxDoneOffset + kRingEntries * sizeof(Slot) <= kRingBytes);
inline constexpr uint32_t kMinBufferSize = 1518;
inline constexpr uint32_t kMinBuffers = 64;
inline constexpr uint32_t kMaxBuffers = 1024;

}  // namespace abi::net
