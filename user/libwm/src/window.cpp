#include <charm/window.hpp>
#include <charm/libc.h>
#include <lib/input/ring.hpp>
#include <string.h>

namespace charm::gui {
namespace wm = abi::wm;
namespace sc = abi::syscall;
namespace {
void close_handles(Handle* hs, uint32_t n) { for (uint32_t i = 0; i < n; ++i) (void)handle_close(hs[i]); }
}
void Window::close() {
  if (channel_ != 0) {
    if (id_ != 0) { wm::Destroy m{wm::kDestroy, id_}; (void)channel_write(channel_, &m, sizeof m, nullptr, 0); }
    (void)handle_close(channel_); channel_ = 0;
  }
  for (auto& p : pixels_) { if (p != nullptr) (void)vmo_unmap(reinterpret_cast<uint64_t>(p)); p = nullptr; }
  if (ring_ != nullptr) (void)vmo_unmap(reinterpret_cast<uint64_t>(ring_));
  ring_ = nullptr; input_ = nullptr; id_ = 0; owned_[0] = owned_[1] = false;
}
Status Window::create(const char* title, uint32_t width, uint32_t height) {
  close();
  if (title == nullptr || width == 0 || height == 0 || width > 16384 || height > 16384) return Status::InvalidArgs;
  Handle door = charm_wm(), pair[2]{};
  if (door == 0) return Status::BadHandle;
  Status s = channel_create(pair);
  if (s != Status::Ok) return s;
  channel_ = pair[0];
  wm::Helo hello{wm::kHelo, wm::kVersion};
  s = channel_write(door, &hello, sizeof hello, &pair[1], 1);
  if (s != Status::Ok) { (void)handle_close(pair[1]); close(); return s; }
  // Bound the handshake; no compositor response must not hang app startup forever.
  uint64_t deadline = now_ns() + 5'000'000'000;
  wm::Welcome welcome{}; Handle hs[4]{};
  auto r = channel_read(channel_, &welcome, sizeof welcome, hs, 4, deadline);
  if (r.ok()) close_handles(hs, static_cast<uint32_t>(r.value));
  if (!r.ok() || (r.value >> 32) != sizeof welcome || static_cast<uint32_t>(r.value) != 0 ||
      welcome.type != wm::kWelcome || welcome.version != wm::kVersion ||
      (welcome.format != sc::kDisplayBgrx && welcome.format != sc::kDisplayRgbx) ||
      width > welcome.screen_w || height > welcome.screen_h) {
    s = r.ok() ? Status::InvalidArgs : r.status; close(); return s;
  }
  format_ = welcome.format; width_ = width; height_ = height;
  uint64_t bytes = (uint64_t{width} * height * 4 + 4095) & ~uint64_t{4095};
  Handle give[2]{};
  for (uint32_t i = 0; i < 2; ++i) {
    auto v = vmo_create(bytes);
    if (!v.ok()) { close_handles(give, i); close(); return v.status; }
    Handle h = static_cast<Handle>(v.value);
    auto map = vmo_map(h, rights::kRead | rights::kWrite);
    auto dup = map.ok() ? handle_duplicate(h, rights::kRead | rights::kMap | rights::kTransfer) : map;
    (void)handle_close(h);
    if (map.ok()) pixels_[i] = reinterpret_cast<uint32_t*>(map.value);
    if (!map.ok() || !dup.ok()) { close_handles(give, i); close(); return map.ok() ? dup.status : map.status; }
    give[i] = static_cast<Handle>(dup.value); owned_[i] = true;
  }
  wm::Create m{wm::kCreate, width, height, {}};
  strncpy(m.title, title, sizeof m.title - 1);
  s = channel_write(channel_, &m, sizeof m, give, 2);
  if (s != Status::Ok) { close_handles(give, 2); close(); return s; }
  wm::Created created{};
  r = channel_read(channel_, &created, sizeof created, hs, 4, deadline);
  uint32_t nh = r.ok() ? static_cast<uint32_t>(r.value) : 0;
  if (!r.ok() || (r.value >> 32) != sizeof created || created.type != wm::kCreated || created.window == 0 || nh != 1) {
    close_handles(hs, nh); s = r.ok() ? Status::InvalidArgs : r.status; close(); return s;
  }
  auto map = vmo_map(hs[0], rights::kRead | rights::kWrite);
  close_handles(hs, nh);
  if (!map.ok()) { close(); return map.status; }
  ring_ = reinterpret_cast<sc::InputRingHeader*>(map.value);
  if (memcmp(ring_->magic, "INPR", 4) != 0 || ring_->version != 1 || ring_->capacity != 128) { close(); return Status::InvalidArgs; }
  input_ = reinterpret_cast<sc::InputEvent*>(map.value + sizeof *ring_);
  id_ = created.window; x_ = created.x; y_ = created.y;
  return Status::Ok;
}
Status Window::present(uint32_t buffer, Damage d) {
  if (id_ == 0 || buffer > 1 || !owned_[buffer] || d.x < 0 || d.y < 0 || d.w <= 0 || d.h <= 0 ||
      uint64_t{static_cast<uint32_t>(d.x)} + static_cast<uint32_t>(d.w) > width_ ||
      uint64_t{static_cast<uint32_t>(d.y)} + static_cast<uint32_t>(d.h) > height_) return Status::InvalidArgs;
  wm::Present p{wm::kPresent, id_, buffer, d.x, d.y, d.w, d.h};
  Status s = channel_write(channel_, &p, sizeof p, nullptr, 0);
  if (s == Status::Ok) owned_[buffer] = false;
  return s;
}
Status Window::wait(wm::WindowEvent* event, uint64_t deadline) {
  if (event == nullptr || id_ == 0) return Status::InvalidArgs;
  Handle hs[4]{};
  auto r = channel_read(channel_, event, sizeof *event, hs, 4, deadline);
  if (!r.ok()) return r.status;
  uint32_t nh = static_cast<uint32_t>(r.value); close_handles(hs, nh);
  if ((r.value >> 32) != sizeof *event || nh != 0 || event->window != id_ || event->type < wm::kFrameDone || event->type > wm::kCloseRequest) return Status::InvalidArgs;
  if (event->type == wm::kFrameDone) {
    if (event->value > 1) return Status::InvalidArgs;
    owned_[event->value] = true;
  }
  return Status::Ok;
}
bool Window::next_input(sc::InputEvent* event) {
  if (ring_ == nullptr || event == nullptr) return false;
  uint32_t head = __atomic_load_n(&ring_->head, __ATOMIC_RELAXED);
  uint32_t tail = __atomic_load_n(&ring_->tail, __ATOMIC_SEQ_CST);
  if (head == tail || tail - head > 128) return false;
  *event = input_[head % 128];
  (void)lib::input::consume_head(&ring_->head, &ring_->tail, head + 1);
  return true;  // caller drains until false before waiting again
}
}  // namespace charm::gui
