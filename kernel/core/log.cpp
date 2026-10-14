#include "log.hpp"

#include <arch/x86_64/serial.hpp>

namespace kernel {

namespace {

class SerialSink final : public lib::core::Sink {
 public:
  void write(const char* data, size_t size) override {
    arch::x86_64::Serial::write(data, size);
  }
};

constinit SerialSink g_serial_sink;
constinit sync::Spinlock g_log_lock{sync::LockRank::Log, "log"};

}  // namespace

lib::core::Sink& log_sink() { return g_serial_sink; }
sync::Spinlock& log_lock() { return g_log_lock; }

}  // namespace kernel
