#pragma once
#include <stddef.h>
#include <stdint.h>

// Intel xHCI 1.2: sections 4.9, 6.4, 6.5 (docs/specs/xhci-1.2.pdf).
namespace lib::usb::xhci {
struct Trb { uint64_t parameter; uint32_t status, control; };
struct ErstEntry { uint64_t base; uint32_t size, reserved; };
static_assert(sizeof(Trb) == 16 && offsetof(Trb, control) == 12);
static_assert(sizeof(ErstEntry) == 16 && offsetof(ErstEntry, size) == 8);
inline constexpr uint32_t kLink = 6, kNoop = 23, kCommandCompletion = 33;
inline constexpr uint32_t type(uint32_t v) { return v << 10; }

// One 4 KiB segment, 255 data TRBs + Link. Caller serializes requests and
// waits for completion before reusing entries (no ring-overrun tracking here).
struct Producer {
  volatile Trb* entries = nullptr;
  uint64_t iova = 0;
  uint32_t index = 0, cycle = 1;
  uint64_t push(Trb trb) {
    uint64_t pointer = iova + uint64_t{index} * 16;
    volatile Trb& out = entries[index++];
    out.parameter = trb.parameter; out.status = trb.status;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    out.control = (trb.control & ~1u) | cycle;
    if (index == 255) {
      volatile Trb& link = entries[255];
      link.parameter = iova; link.status = 0;
      __atomic_thread_fence(__ATOMIC_RELEASE);
      link.control = type(kLink) | 2 | cycle;  // Toggle Cycle
      index = 0; cycle ^= 1;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return pointer;
  }
};
struct Consumer {
  volatile Trb* entries = nullptr;
  uint64_t iova = 0;
  uint32_t index = 0, cycle = 1;
  bool pop(Trb* out) {
    const volatile Trb& e = entries[index];
    uint32_t control = e.control;
    if ((control & 1) != cycle) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *out = {e.parameter, e.status, control};
    if (++index == 256) { index = 0; cycle ^= 1; }
    return true;
  }
  uint64_t dequeue() const { return iova + uint64_t{index} * 16; }
};
}  // namespace lib::usb::xhci
