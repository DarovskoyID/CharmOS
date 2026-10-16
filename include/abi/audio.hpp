#pragma once
#include <stdint.h>
namespace abi::audio {
inline constexpr uint32_t kVersion = 1, kHelo = 1, kPlay = 2, kWelcome = 101, kDone = 102;
inline constexpr uint32_t kRate = 48000, kChannels = 2, kBits = 16, kMaxFrames = 1024;
struct Helo { uint32_t type, version; };
struct Welcome { uint32_t type, version, rate, channels, bits, max_frames; };
struct Play { uint32_t type, frames; };
struct Done { uint32_t type, frames; int32_t status; };
static_assert(sizeof(Helo) == 8 && sizeof(Welcome) == 24 && sizeof(Play) == 8 && sizeof(Done) == 12);
static_assert(__builtin_offsetof(Welcome, rate) == 8 && __builtin_offsetof(Done, status) == 8);
}  // namespace abi::audio
