// Intel High Definition Audio 1.0a, chapters 3 (controller) and 7 (codec).
// PCM output only, 48 kHz stereo s16; serialized bounded periods (audio.md).
#include <abi/audio.hpp>
#include <lib/audio/hda.hpp>
#include "../../tests/check.hpp"

using namespace charm;
namespace a = abi::audio;
namespace h = lib::audio::hda;
namespace {
uint64_t base, stream;
Handle device, irq, port;
uint32_t stream_index, codec, group, dac, pin;
volatile uint32_t* corb;
volatile uint64_t* rirb;
uint32_t corb_mask, rirb_mask, corb_wp = 0, rirb_rp = 0;
uint64_t pcm_cpu, pcm_iova, bdl_cpu, bdl_iova;
uint32_t widgets[256]{};
bool present[256]{}, visiting[256]{};

template <typename T> volatile T& reg(uint64_t offset) { return *reinterpret_cast<volatile T*>(base + offset); }
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
void log(const char* text, uint64_t value) {
  char out[128]; auto s = lib::core::format_to(out, sizeof out, "hda: {} {}\n", text, value);
  debug_write(s.data(), s.size());
}
template <typename T> bool wait(uint64_t offset, T mask, T value) {
  uint64_t deadline = now_ns() + 200'000'000;
  while ((reg<T>(offset) & mask) != value) { if (now_ns() >= deadline) return false; yield(); }
  return true;
}
bool dma(uint64_t bytes, uint64_t* cpu, uint64_t* iova) {
  auto v = dma_alloc(device, bytes, abi::syscall::kDmaContiguous, iova);
  if (!v.ok()) return false;
  auto m = vmo_map(static_cast<Handle>(v.value), rights::kRead | rights::kWrite); (void)handle_close(static_cast<Handle>(v.value));
  if (!m.ok() || ((reg<uint16_t>(0) & 1) == 0 && *iova + bytes > 0x100000000ull)) return false;
  *cpu = m.value; return true;
}
bool command(uint32_t raw, uint32_t* response) {
  corb_wp = (corb_wp + 1) & corb_mask;
  corb[corb_wp] = raw; fence(); reg<uint16_t>(0x48) = static_cast<uint16_t>(corb_wp);
  uint64_t deadline = now_ns() + 200'000'000;
  for (;;) {
    uint32_t wp = reg<uint16_t>(0x58) & rirb_mask;
    while (rirb_rp != wp) {
      rirb_rp = (rirb_rp + 1) & rirb_mask; fence();
      uint64_t entry = rirb[rirb_rp];
      if (((entry >> 32) & 0x1f) == codec) {
        reg<uint8_t>(0x5d) = 1; fence();
        *response = static_cast<uint32_t>(entry); return true;
      }
    }
    if (now_ns() >= deadline) {
      log("command timed out", raw);
      log("CORB pointers", (uint64_t{reg<uint16_t>(0x48)} << 32) | reg<uint16_t>(0x4a));
      log("RIRB status", (uint64_t{reg<uint16_t>(0x58)} << 32) | reg<uint8_t>(0x5d));
      return false;
    }
    yield();
  }
}
bool verb(uint32_t node, uint32_t id, uint32_t payload, uint32_t* response) {
  return command(h::verb(codec, node, id, payload), response);
}
bool set(uint32_t node, uint32_t id, uint32_t payload) { uint32_t ignored; return verb(node, id, payload, &ignored); }
bool parameter(uint32_t node, uint32_t id, uint32_t* value) { return verb(node, 0xf00, id, value); }
bool unmute(uint32_t node, bool output, uint32_t index = 0) {
  uint32_t caps = widgets[node];
  if ((caps & (output ? 4u : 2u)) == 0) return true;
  // Set Amplifier Gain/Mute (7.3.3.7): input index occupies bits 11:8.
  if (!output && index > 15) return false;
  uint32_t amp;
  if (!parameter((caps & 8) != 0 ? node : group, output ? 0x12u : 0xdu, &amp)) return false;
  uint32_t gain = amp & 0x7f, maximum = (amp >> 8) & 0x7f;
  if (gain > maximum) gain = maximum;
  return set(node, 0x300, (output ? 0xb000u : 0x7000u) | (index << 8) | gain);
}
bool route(uint32_t node, unsigned depth) {
  if (node == 0 || node > 255 || !present[node] || visiting[node] || depth > 16) return false;
  visiting[node] = true;
  uint32_t caps = widgets[node], type = (caps >> 20) & 15;
  if ((caps & (1u << 10)) != 0 && !set(node, 0x705, 0)) { visiting[node] = false; return false; }
  if (type == 0) {
    uint32_t pcm, formats;
    bool ok = (caps & 1) != 0 && (caps & (1u << 9)) == 0 &&
              parameter((caps & 16) != 0 ? node : group, 0xb, &formats) && (formats & 1) != 0 &&
              parameter((caps & 16) != 0 ? node : group, 0xa, &pcm) &&
              (pcm & ((1u << 6) | (1u << 17))) == ((1u << 6) | (1u << 17));
    if (ok) { dac = node; ok = unmute(node, true); }
    visiting[node] = false; return ok;
  }
  if (type != 2 && type != 3 && type != 4) { visiting[node] = false; return false; }
  uint32_t length;
  if (!parameter(node, 0xe, &length)) { visiting[node] = false; return false; }
  bool long_form = (length & 0x80) != 0;
  uint32_t entries = length & 0x7f, count = 0; uint16_t previous = 0; uint8_t list[32]{};
  for (uint32_t i = 0; i < entries;) {
    uint32_t raw;
    if (!verb(node, 0xf02, i, &raw)) { visiting[node] = false; return false; }
    uint32_t step = long_form ? 2u : 4u, bits = long_form ? 16u : 8u;
    for (uint32_t j = 0; j < step && i < entries; ++j, ++i) {
      uint16_t item = static_cast<uint16_t>((raw >> (j * bits)) & (long_form ? 0xffffu : 0xffu));
      if (!h::connection(item, long_form, &previous, list, &count, 32)) { visiting[node] = false; return false; }
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (!route(list[i], depth + 1)) continue;
    bool ok = (count == 1 || set(node, 0x701, i)) && unmute(node, false, i) && unmute(node, true);
    visiting[node] = false; return ok;
  }
  visiting[node] = false; return false;
}
bool discover() {
  uint32_t nodes;
  if (!parameter(0, 4, &nodes)) return false;
  uint32_t first = (nodes >> 16) & 255, count = nodes & 255;
  for (uint32_t g = first; g < first + count && g < 256; ++g) {
    uint32_t type;
    if (!parameter(g, 5, &type) || (type & 255) != 1) continue;
    group = g;
    if (!set(group, 0x705, 0) || !parameter(group, 4, &nodes)) continue;
    first = (nodes >> 16) & 255; count = nodes & 255;
    for (uint32_t n = first; n < first + count && n < 256; ++n) {
      present[n] = parameter(n, 9, &widgets[n]);
    }
    for (uint32_t n = first; n < first + count && n < 256; ++n) {
      if (!present[n] || ((widgets[n] >> 20) & 15) != 4 || (widgets[n] & (1u << 9)) != 0) continue;
      uint32_t pincap, defaults;
      if (!parameter(n, 0xc, &pincap) || !verb(n, 0xf1c, 0, &defaults)) continue;
      if ((pincap & 16) == 0 || (defaults >> 30) == 1) continue;
      if (!route(n, 0)) continue;
      pin = n;
      if (!set(pin, 0x707, 0x40u | ((pincap & 8) != 0 ? 0x80u : 0u)) || ((pincap & (1u << 16)) != 0 && !set(pin, 0x70c, 2)) ||
          !set(dac, 0x706, 0x10) || !set(dac, 0x200, h::kPcm48Stereo16)) return false;
      log("output pin", pin); log("DAC", dac); return true;
    }
    return false;
  }
  return false;
}
Status play(const uint8_t* data, uint32_t frames) {
  reg<uint8_t>(stream) = 0;
  if (!wait<uint8_t>(stream, 2, 0)) return Status::IoError;
  for (uint32_t i = 0; i < 4096; ++i) reinterpret_cast<uint8_t*>(pcm_cpu)[i] = i < frames * 4 ? data[i] : 0;
  reg<uint8_t>(stream) = 1; if (!wait<uint8_t>(stream, 1, 1)) return Status::IoError;
  reg<uint8_t>(stream) = 0; if (!wait<uint8_t>(stream, 1, 0)) return Status::IoError;
  auto* bdl = reinterpret_cast<h::Bdle*>(bdl_cpu);
  bdl[0] = {pcm_iova, 2048, 0}; bdl[1] = {pcm_iova + 2048, 2048, 1}; fence();
  reg<uint32_t>(stream + 8) = 4096; reg<uint16_t>(stream + 0xc) = 1;
  reg<uint16_t>(stream + 0x12) = h::kPcm48Stereo16;
  reg<uint32_t>(stream + 0x18) = static_cast<uint32_t>(bdl_iova);
  reg<uint32_t>(stream + 0x1c) = static_cast<uint32_t>(bdl_iova >> 32);
  reg<uint8_t>(stream + 2) = 0x10; reg<uint8_t>(stream + 3) = 0x1c;
  reg<uint32_t>(0x20) = 0x80000000u | (1u << stream_index);
  (void)object_bind(port, irq, 1); fence(); reg<uint8_t>(stream) = 0x1e;
  uint64_t deadline = now_ns() + 500'000'000;
  Status result = Status::TimedOut;
  for (;;) {
    PortPacket packet{}; Status s = port_wait(port, &packet, deadline);
    if (s == Status::TimedOut || now_ns() >= deadline) break;
    if (s != Status::Ok || packet.key != 1) continue;
    uint8_t status = reg<uint8_t>(stream + 3);
    reg<uint8_t>(stream + 3) = status & 0x1c; fence();
    (void)syscall1(Number::InterruptAck, irq);
    if ((status & 0x18) != 0) { result = Status::IoError; break; }
    if ((status & 4) != 0) { result = Status::Ok; break; }
    (void)object_bind(port, irq, 1);
  }
  reg<uint8_t>(stream) = 0; fence();
  return result;
}
struct Client { Handle channel; } clients[8]{};
void serve(Handle kernel, Handle door) {
  // Drain even invalid maximum-size IPC messages so a malformed HELO cannot
  // poison the shared door with a permanent BufferTooSmall result.
  alignas(8) static uint8_t message[abi::syscall::kChannelMaxBytes]; Handle hs[16];
  for (;;) {
    (void)object_bind(port, kernel, 2); (void)object_bind(port, door, 3);
    for (uint32_t i = 0; i < 8; ++i) if (clients[i].channel != 0) (void)object_bind(port, clients[i].channel, 100 + i);
    PortPacket packet{};
    if (port_wait(port, &packet, kForever) != Status::Ok) continue;
    if (packet.key == 2 && packet.type == abi::syscall::kPacketPeerClosed) return;
    if (packet.key == 1) { reg<uint8_t>(stream + 3) = 0x1c; (void)syscall1(Number::InterruptAck, irq); continue; }
    if (packet.key == 3) {
      auto r = channel_read(door, message, sizeof message, hs, 16, 0);
      if (!r.ok()) continue;
      uint32_t nh = static_cast<uint32_t>(r.value), len = static_cast<uint32_t>(r.value >> 32);
      auto* hello = reinterpret_cast<a::Helo*>(message);
      int slot = -1; for (int i = 0; i < 8; ++i) if (clients[i].channel == 0) { slot = i; break; }
      if (len != sizeof *hello || nh != 1 || hello->type != a::kHelo || hello->version != a::kVersion || slot < 0) {
        for (uint32_t i = 0; i < nh; ++i) (void)handle_close(hs[i]); continue;
      }
      clients[slot].channel = hs[0];
      a::Welcome welcome{a::kWelcome, a::kVersion, a::kRate, a::kChannels, a::kBits, a::kMaxFrames};
      if (channel_write(hs[0], &welcome, sizeof welcome, nullptr, 0) != Status::Ok ||
          object_bind(port, hs[0], 100 + static_cast<uint64_t>(slot)) != Status::Ok) {
        (void)handle_close(hs[0]); clients[slot].channel = 0;
      }
    } else if (packet.key >= 100 && packet.key < 108) {
      auto& client = clients[packet.key - 100];
      if (client.channel == 0) continue;
      auto r = channel_read(client.channel, message, sizeof message, hs, 16, 0);
      uint32_t nh = r.ok() ? static_cast<uint32_t>(r.value) : 0, len = r.ok() ? static_cast<uint32_t>(r.value >> 32) : 0;
      for (uint32_t i = 0; i < nh; ++i) (void)handle_close(hs[i]);
      auto* request = reinterpret_cast<a::Play*>(message);
      if (!r.ok() || nh != 0 || len < sizeof *request || request->type != a::kPlay || request->frames == 0 ||
          request->frames > a::kMaxFrames || len != sizeof *request + request->frames * 4) {
        (void)handle_close(client.channel); client.channel = 0; continue;
      }
      Status status = play(message + sizeof *request, request->frames);
      a::Done done{a::kDone, request->frames, static_cast<int32_t>(status)};
      (void)channel_write(client.channel, &done, sizeof done, nullptr, 0);
      if (status == Status::Ok) log("PCM frames played", request->frames);
    }
  }
}
}

extern "C" int64_t charm_main(Handle kernel) {
  char message[16]; Handle hs[1]; uint32_t n, nh;
  EXPECT(read_msg(kernel, message, sizeof message, hs, 1, &n, &nh) == Status::Ok && same(message, n, "device") && nh == 1);
  device = hs[0]; abi::syscall::DeviceInfo info{};
  EXPECT(syscall2(Number::DeviceInfo, device, reinterpret_cast<uint64_t>(&info)).ok());
  EXPECT(info.class_code == 0x040300 && info.irq_count > 0 && info.bar_size[0] >= 0x4000);
  auto bar = syscall2(Number::DeviceMapBar, device, 0); EXPECT(bar.ok());
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite); EXPECT(map.ok()); base = map.value;
  (void)handle_close(static_cast<Handle>(bar.value));
  reg<uint32_t>(0x20) = 0; reg<uint8_t>(0x4c) = 0; reg<uint8_t>(0x5c) = 0;
  uint16_t gcap = reg<uint16_t>(0); uint32_t inputs = (gcap >> 8) & 15, outputs = (gcap >> 12) & 15;
  EXPECT(outputs > 0); stream_index = inputs; stream = 0x80 + uint64_t{stream_index} * 32;
  uint32_t total_streams = inputs + outputs + ((gcap >> 3) & 31);
  for (uint32_t i = 0; i < total_streams; ++i) {
    uint64_t offset = 0x80 + uint64_t{i} * 32;
    reg<uint8_t>(offset) = 0; EXPECT(wait<uint8_t>(offset, 2, 0));
  }
  EXPECT(wait<uint8_t>(0x4c, 2, 0) && wait<uint8_t>(0x5c, 2, 0));
  reg<uint32_t>(8) = 0; EXPECT(wait<uint32_t>(8, 1, 0)); sleep_until(now_ns() + 100'000);
  reg<uint32_t>(8) = 1; EXPECT(wait<uint32_t>(8, 1, 1)); sleep_until(now_ns() + 1'000'000);
  uint16_t state = reg<uint16_t>(0xe) & 0x7fff; EXPECT(state != 0);
  codec = static_cast<uint32_t>(__builtin_ctz(static_cast<unsigned>(state)));
  uint64_t ring_cpu, ring_iova; EXPECT(dma(4096, &ring_cpu, &ring_iova));
  auto choose_size = [](uint8_t capability, uint32_t* mask) -> uint8_t {
    if ((capability & 0x40) != 0) { *mask = 255; return 2; }
    if ((capability & 0x20) != 0) { *mask = 15; return 1; }
    *mask = 1; return 0;
  };
  uint8_t corb_size = reg<uint8_t>(0x4e), rirb_size = reg<uint8_t>(0x5e);
  EXPECT((corb_size & 0x70) != 0 && (rirb_size & 0x70) != 0);
  reg<uint8_t>(0x4e) = static_cast<uint8_t>((corb_size & ~3u) | choose_size(corb_size, &corb_mask));
  reg<uint8_t>(0x5e) = static_cast<uint8_t>((rirb_size & ~3u) | choose_size(rirb_size, &rirb_mask));
  corb = reinterpret_cast<volatile uint32_t*>(ring_cpu); rirb = reinterpret_cast<volatile uint64_t*>(ring_cpu + 2048);
  reg<uint32_t>(0x40) = static_cast<uint32_t>(ring_iova); reg<uint32_t>(0x44) = static_cast<uint32_t>(ring_iova >> 32);
  reg<uint32_t>(0x50) = static_cast<uint32_t>(ring_iova + 2048); reg<uint32_t>(0x54) = static_cast<uint32_t>((ring_iova + 2048) >> 32);
  reg<uint16_t>(0x4a) = 0x8000; EXPECT(wait<uint16_t>(0x4a, 0x8000, 0x8000));
  reg<uint16_t>(0x4a) = 0; EXPECT(wait<uint16_t>(0x4a, 0x8000, 0));
  reg<uint16_t>(0x48) = 0; reg<uint16_t>(0x58) = 0x8000; reg<uint16_t>(0x5a) = 1;
  // Count and acknowledge each response; INTCTL.CIE remains disabled because
  // codec replies are polled. This also rearms response-count backpressure.
  reg<uint8_t>(0x5d) = 5; reg<uint8_t>(0x5c) = 3; reg<uint8_t>(0x4c) = 2; fence();
  uint32_t vendor; EXPECT(parameter(0, 0, &vendor)); log("codec vendor", vendor);
  EXPECT(discover()); EXPECT(dma(4096, &pcm_cpu, &pcm_iova) && dma(4096, &bdl_cpu, &bdl_iova));
  auto interrupt = syscall2(Number::DeviceIrq, device, 0); EXPECT(interrupt.ok()); irq = static_cast<Handle>(interrupt.value);
  auto p = port_create(); EXPECT(p.ok()); port = static_cast<Handle>(p.value);
  Handle pair[2]; EXPECT(channel_create(pair) == Status::Ok);
  EXPECT(channel_write(kernel, "aud1", 4, &pair[1], 1) == Status::Ok);
  print("hda: PCM ready (48000 stereo s16)\n"); serve(kernel, pair[0]);
  return 0;
}
