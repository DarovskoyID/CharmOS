#pragma once
#include <abi/wm.hpp>
#include <charm/sys.hpp>

// Client library for the existing wm v2 wire protocol; no new syscall ABI.
namespace charm::gui {
struct Damage { int32_t x, y, w, h; };
class Window {
 public:
  Window() = default;
  ~Window() { close(); }
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Status create(const char* title, uint32_t width, uint32_t height);
  void close();
  Status present(uint32_t buffer, Damage damage);
  Status wait(abi::wm::WindowEvent* event, uint64_t deadline = kForever);
  bool next_input(abi::syscall::InputEvent* event);
  uint32_t* pixels(uint32_t buffer) const { return buffer < 2 && owned_[buffer] ? pixels_[buffer] : nullptr; }
  uint32_t id() const { return id_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  uint32_t format() const { return format_; }
 private:
  Handle channel_ = 0;
  uint32_t id_ = 0, format_ = 0, width_ = 0, height_ = 0;
  int32_t x_ = 0, y_ = 0;
  uint32_t* pixels_[2] = {};
  bool owned_[2] = {};
  abi::syscall::InputRingHeader* ring_ = nullptr;
  abi::syscall::InputEvent* input_ = nullptr;
};
}  // namespace charm::gui
