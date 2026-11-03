#pragma once
#include <abi/wm.hpp>
#include <charm/sys.hpp>
#include <stddef.h>

// Native WM v2 and explicit managed v3 clients; no new syscall ABI.
namespace charm::gui {
struct Damage { int32_t x, y, w, h; };
struct WindowBounds { int32_t x,y; uint32_t width,height; };
struct WindowDisplay {
  uint32_t width,height,format,title_height,features;
  WindowBounds work;
};
class Window {
 public:
  Window() = default;
  ~Window() { close(); }
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Status create(const char* title, uint32_t width, uint32_t height);
  Status create_managed(const char* title,uint32_t width,uint32_t height,uint32_t flags=0);
  Status request_bounds(int32_t x,int32_t y,uint32_t width,uint32_t height);
  Status set_position(int32_t x,int32_t y);
  Status set_title(const char* title);
  Status set_visible(bool visible,bool activate=true);
  Status activate(bool active=true);
  void close();
  Status present(uint32_t buffer, Damage damage);
  Status present_pixels(const void* source,size_t source_bytes,uint32_t stride_bytes,
                        uint32_t format,Damage damage,uint32_t* submitted=nullptr);
  Status wait(abi::wm::WindowEvent* event, uint64_t deadline = kForever);
  bool next_input(abi::syscall::InputEvent* event);
  uint32_t* pixels(uint32_t buffer) const { return buffer < 2 && !pending_generation_ && owned_[buffer] ? pixels_[buffer] : nullptr; }
  uint32_t id() const { return id_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  uint32_t format() const { return format_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t protocol_version() const { return protocol_; }
  uint32_t generation() const { return generation_; }
  bool configuration_pending() const { return pending_generation_!=0; }
  bool visible() const { return visible_; }
  bool active() const { return active_; }
  const WindowDisplay& display() const { return display_; }
  // Borrowed readiness handle. Consume events only through wait(), which also
  // updates buffer ownership; never close/transfer this borrowed capability.
  Handle event_handle() const { return channel_; }
 private:
  Status create_impl(const char* title,uint32_t width,uint32_t height,uint32_t version,uint32_t flags);
  bool supports(uint32_t feature) const { return protocol_==abi::wm::kManagedVersion && (display_.features&feature); }
  void discard_pending();
  Handle channel_ = 0;
  uint32_t id_ = 0, format_ = 0, width_ = 0, height_ = 0;
  int32_t x_ = 0, y_ = 0;
  uint32_t* pixels_[2] = {};
  bool owned_[2] = {};
  uint32_t protocol_=0,generation_=0,pending_generation_=0;
  uint32_t pending_width_=0,pending_height_=0;
  uint32_t* pending_pixels_[2]={};
  WindowDisplay display_{};
  bool visible_=false,active_=false;
  abi::syscall::InputRingHeader* ring_ = nullptr;
  abi::syscall::InputEvent* input_ = nullptr;
};
}  // namespace charm::gui
