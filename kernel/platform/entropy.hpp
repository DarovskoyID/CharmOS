#pragma once
#include <abi/syscall.hpp>
#include <stddef.h>
#include <stdint.h>
namespace kernel::platform {
abi::syscall::Status random_bytes(uint8_t* out, size_t size, bool nonblock);
}
