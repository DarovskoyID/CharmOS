#include <abi/audio.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <stdio.h>
#include <unistd.h>

using namespace charm;
namespace a = abi::audio;
namespace {
Handle connect(Handle door) {
  Handle pair[2]; if (channel_create(pair) != Status::Ok) return 0;
  a::Helo hello{a::kHelo, a::kVersion};
  if (channel_write(door, &hello, sizeof hello, &pair[1], 1) != Status::Ok) {
    (void)handle_close(pair[0]); (void)handle_close(pair[1]); return 0;
  }
  a::Welcome welcome{}; Handle hs[1];
  auto r = channel_read(pair[0], &welcome, sizeof welcome, hs, 1, now_ns() + 2'000'000'000);
  if (!r.ok() || r.value != (uint64_t{sizeof welcome} << 32) || welcome.type != a::kWelcome ||
      welcome.version != a::kVersion || welcome.rate != a::kRate || welcome.channels != 2 || welcome.bits != 16) {
    (void)handle_close(pair[0]); return 0;
  }
  return pair[0];
}
}
extern "C" int main(int, char**) {
  Handle token = charm_audio();
  if (token == 0 || syscall1(abi::syscall::Number::AudioOpen, 0).status != Status::BadHandle) return 1;
  auto narrow = handle_duplicate(token, rights::kDuplicate | rights::kTransfer);
  if (!narrow.ok() || syscall1(abi::syscall::Number::AudioOpen, narrow.value).status != Status::AccessDenied) return 1;
  (void)handle_close(static_cast<Handle>(narrow.value));
  SysResult opened{Status::NotFound, 0};
  uint64_t deadline = now_ns() + 5'000'000'000;
  while (opened.status == Status::NotFound && now_ns() < deadline) {
    opened = syscall1(abi::syscall::Number::AudioOpen, token);
    if (opened.status == Status::NotFound) usleep(10'000);
  }
  if (!opened.ok()) { printf("audio_test: audio_open failed\n"); return 1; }
  Handle door = static_cast<Handle>(opened.value), channel = connect(door);
  if (channel == 0) return 1;
  a::Play invalid{a::kPlay, 0};
  if (channel_write(channel, &invalid, sizeof invalid, nullptr, 0) != Status::Ok) return 1;
  a::Done done{}; Handle hs[1];
  if (channel_read(channel, &done, sizeof done, hs, 1, now_ns() + 2'000'000'000).status != Status::PeerClosed) return 1;
  (void)handle_close(channel); channel = connect(door); if (channel == 0) return 1;
  struct Packet { a::Play header; int16_t samples[2048]; } packet{};
  static_assert(sizeof(Packet) == 4104 && __builtin_offsetof(Packet, samples) == 8);
  const uint32_t sizes[] = {a::kMaxFrames, 128};
  for (uint32_t frames : sizes) {
    packet.header = {a::kPlay, frames};
    for (uint32_t i = 0; i < frames * 2; ++i) packet.samples[i] = static_cast<int16_t>((static_cast<int32_t>(i % 64) - 32) * 64);
    if (channel_write(channel, &packet, sizeof packet.header + frames * 4, nullptr, 0) != Status::Ok) return 1;
    auto r = channel_read(channel, &done, sizeof done, hs, 1, now_ns() + 2'000'000'000);
    if (!r.ok() || r.value != (uint64_t{sizeof done} << 32) || done.type != a::kDone || done.frames != frames || done.status != 0) {
      printf("audio_test: DMA playback failed (%d)\n", done.status); return 1;
    }
  }
  (void)handle_close(channel); (void)handle_close(door);
  printf("audio_test: PCM playback and access checks verified\n");
  return 0;
}
