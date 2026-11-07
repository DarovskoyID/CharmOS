#include <charm/window.hpp>
#include <charm/libc.h>
#include <lib/input/ring.hpp>
#include <string.h>

namespace charm::gui {
namespace wm = abi::wm;
namespace sc = abi::syscall;
namespace {
void close_handles(Handle* hs, uint32_t n) { for (uint32_t i = 0; i < n; ++i) (void)handle_close(hs[i]); }
void unmap_pixels(uint32_t** pixels) {
  for(unsigned i=0;i<2;++i) {
    if(pixels[i]) (void)vmo_unmap(reinterpret_cast<uint64_t>(pixels[i]));
    pixels[i]=nullptr;
  }
}
Status allocate_pixels(uint32_t width,uint32_t height,uint32_t** pixels,Handle* give) {
  uint64_t bytes=(uint64_t{width}*height*4+4095)&~uint64_t{4095};
  for(unsigned i=0;i<2;++i) {
    auto memory=vmo_create(bytes);
    if(!memory.ok()) {
      close_handles(give,i); unmap_pixels(pixels); return memory.status;
    }
    Handle handle=static_cast<Handle>(memory.value);
    auto mapping=vmo_map(handle,rights::kRead|rights::kWrite);
    auto duplicate=mapping.ok() ? handle_duplicate(handle,rights::kRead|rights::kMap|rights::kTransfer) : mapping;
    (void)handle_close(handle);
    if(mapping.ok()) pixels[i]=reinterpret_cast<uint32_t*>(mapping.value);
    if(!mapping.ok() || !duplicate.ok()) {
      close_handles(give,i); unmap_pixels(pixels); return mapping.ok() ? duplicate.status : mapping.status;
    }
    give[i]=static_cast<Handle>(duplicate.value);
  }
  return Status::Ok;
}
bool valid_damage(Damage damage,uint32_t width,uint32_t height) {
  return damage.x>=0 && damage.y>=0 && damage.w>0 && damage.h>0 &&
      uint64_t{static_cast<uint32_t>(damage.x)}+static_cast<uint32_t>(damage.w)<=width &&
      uint64_t{static_cast<uint32_t>(damage.y)}+static_cast<uint32_t>(damage.h)<=height;
}
}
void Window::close() {
  if (channel_ != 0) {
    if (id_ != 0) { wm::Destroy m{wm::kDestroy, id_}; (void)channel_write(channel_, &m, sizeof m, nullptr, 0); }
    (void)handle_close(channel_); channel_ = 0;
  }
  unmap_pixels(pixels_);
  discard_pending();
  if (ring_ != nullptr) (void)vmo_unmap(reinterpret_cast<uint64_t>(ring_));
  ring_ = nullptr; input_ = nullptr; id_ = 0; owned_[0] = owned_[1] = false;
  width_=0; height_=0; format_=0; x_=0; y_=0;
  protocol_=0; generation_=0; display_={}; visible_=active_=captured_=false;
}
Status Window::create(const char* title, uint32_t width, uint32_t height) {
  return create_impl(title,width,height,wm::kVersion,0);
}
Status Window::create_managed(const char* title,uint32_t width,uint32_t height,uint32_t flags) {
  if(flags&~(wm::kCreateInvisible|wm::kCreateInactive)) return Status::InvalidArgs;
  return create_impl(title,width,height,wm::kManagedVersion,flags);
}
Status Window::create_impl(const char* title,uint32_t width,uint32_t height,uint32_t version,uint32_t flags) {
  close();
  if (title == nullptr || width == 0 || height == 0 || width > 16384 || height > 16384) return Status::InvalidArgs;
  Handle door = charm_wm(), pair[2]{};
  if (door == 0) return Status::BadHandle;
  Status s = channel_create(pair);
  if (s != Status::Ok) return s;
  channel_ = pair[0];
  wm::Helo hello{wm::kHelo,version};
  s = channel_write(door, &hello, sizeof hello, &pair[1], 1);
  if (s != Status::Ok) { (void)handle_close(pair[1]); close(); return s; }
  // Bound the handshake; no compositor response must not hang app startup forever.
  uint64_t deadline = now_ns() + 5'000'000'000;
  wm::WelcomeV3 extended{}; Handle hs[4]{};
  wm::Welcome& welcome=extended.base;
  auto r = channel_read(channel_, &extended, sizeof extended, hs, 4, deadline);
  if (r.ok()) close_handles(hs, static_cast<uint32_t>(r.value));
  size_t welcome_bytes=version==wm::kManagedVersion ? sizeof extended : sizeof welcome;
  if (!r.ok() || (r.value >> 32) != welcome_bytes || static_cast<uint32_t>(r.value) != 0 ||
      welcome.type != wm::kWelcome || welcome.version != version ||
      (welcome.format != sc::kDisplayBgrx && welcome.format != sc::kDisplayRgbx) ||
      !welcome.screen_w || !welcome.screen_h || welcome.screen_w>16384 || welcome.screen_h>16384) {
    s = r.ok() ? Status::InvalidArgs : r.status; close(); return s;
  }
  display_={welcome.screen_w,welcome.screen_h,welcome.format,0,0,
            {0,0,welcome.screen_w,welcome.screen_h}};
  if(version==wm::kManagedVersion) {
    if(extended.work_x<0 || extended.work_y<0 || !extended.work_width ||
       extended.work_height<=extended.title_height ||
       uint64_t{static_cast<uint32_t>(extended.work_x)}+extended.work_width>welcome.screen_w ||
       uint64_t{static_cast<uint32_t>(extended.work_y)}+extended.work_height>welcome.screen_h) {
      close(); return Status::InvalidArgs;
    }
    display_.title_height=extended.title_height;
    display_.features=extended.features;
    display_.work={extended.work_x,extended.work_y,extended.work_width,extended.work_height};
    if(width>extended.work_width) width=extended.work_width;
    uint32_t maximum_height=extended.work_height-extended.title_height;
    if(height>maximum_height) height=maximum_height;
  } else if(width>welcome.screen_w || height>welcome.screen_h) {
    close(); return Status::InvalidArgs;
  }
  protocol_=version; generation_=version==wm::kManagedVersion ? 1 : 0;
  format_ = welcome.format; width_ = width; height_ = height;
  Handle give[2]{};
  s=allocate_pixels(width,height,pixels_,give);
  if(s!=Status::Ok) { close(); return s; }
  owned_[0]=owned_[1]=true;
  wm::CreateV3 managed{{wm::kCreate,width,height,{}},flags};
  strncpy(managed.base.title,title,sizeof managed.base.title-1);
  size_t create_bytes=version==wm::kManagedVersion ? sizeof managed : sizeof managed.base;
  s = channel_write(channel_, &managed, create_bytes, give, 2);
  if (s != Status::Ok) { close_handles(give, 2); close(); return s; }
  wm::Created created{};
  r = channel_read(channel_, &created, sizeof created, hs, 4, deadline);
  uint32_t nh = r.ok() ? static_cast<uint32_t>(r.value) : 0;
  if (!r.ok() || (r.value >> 32) != sizeof created || created.type != wm::kCreated || created.window == 0 || nh != 1) {
    close_handles(hs, nh); s = r.ok() ? Status::InvalidArgs : r.status; close(); return s;
  }
  auto ring_size=vmo_size(hs[0]);
  if(!ring_size.ok() || ring_size.value<sizeof(sc::InputRingHeader)+128*sizeof(sc::InputEvent)) {
    close_handles(hs,nh); close(); return Status::InvalidArgs;
  }
  auto map = vmo_map(hs[0], rights::kRead | rights::kWrite);
  close_handles(hs, nh);
  if (!map.ok()) { close(); return map.status; }
  ring_ = reinterpret_cast<sc::InputRingHeader*>(map.value);
  if (memcmp(ring_->magic, "INPR", 4) != 0 || ring_->version != 1 || ring_->capacity != 128) { close(); return Status::InvalidArgs; }
  input_ = reinterpret_cast<sc::InputEvent*>(map.value + sizeof *ring_);
  id_ = created.window; x_ = created.x; y_ = created.y;
  // v2's first FRAME_DONE releases buffer 1. Do not allow it to be submitted
  // before that notification and then mistakenly released a second time.
  owned_[1]=false;
  visible_=!(flags&wm::kCreateInvisible);
  return Status::Ok;
}
Status Window::present(uint32_t buffer, Damage d) {
  if (id_ == 0 || buffer > 1 || !valid_damage(d,width_,height_)) return Status::InvalidArgs;
  if(pending_generation_) return Status::WouldBlock;
  if(!owned_[buffer]) return Status::InvalidArgs;
  wm::Present p{wm::kPresent, id_, buffer, d.x, d.y, d.w, d.h};
  wm::PresentV3 managed{p,generation_};
  size_t bytes=protocol_==wm::kManagedVersion ? sizeof managed : sizeof p;
  Status s = channel_write(channel_, &managed, bytes, nullptr, 0);
  if (s == Status::Ok) owned_[buffer] = false;
  return s;
}
Status Window::present_pixels(const void* source,size_t source_bytes,uint32_t stride_bytes,
                              uint32_t source_format,Damage damage,uint32_t* submitted) {
  if(!id_ || !source || !valid_damage(damage,width_,height_) ||
     (source_format!=sc::kDisplayBgrx && source_format!=sc::kDisplayRgbx)) return Status::InvalidArgs;
  uint64_t row_bytes=uint64_t{width_}*4;
  if(stride_bytes<row_bytes) return Status::InvalidArgs;
  uint64_t needed=uint64_t{height_-1}*stride_bytes+row_bytes;
  if(needed>source_bytes) return Status::InvalidArgs;
  uintptr_t source_begin=reinterpret_cast<uintptr_t>(source);
  if(needed>UINTPTR_MAX-source_begin) return Status::InvalidArgs;
  uintptr_t source_end=source_begin+static_cast<uintptr_t>(needed);
  uintptr_t window_bytes=static_cast<uintptr_t>(uint64_t{width_}*height_*4);
  for(const auto* allocation:pixels_) {
    uintptr_t begin=reinterpret_cast<uintptr_t>(allocation);
    if(allocation && source_begin<begin+window_bytes && begin<source_end) return Status::InvalidArgs;
  }
  if(pending_generation_) return Status::WouldBlock;
  uint32_t buffer=owned_[0] ? 0u : (owned_[1] ? 1u : 2u);
  if(buffer==2) return Status::WouldBlock;
  const auto* source_row=static_cast<const unsigned char*>(source);
  for(uint32_t y=0;y<height_;++y) {
    uint32_t* target=pixels_[buffer]+static_cast<size_t>(y)*width_;
    if(source_format==format_) memcpy(target,source_row,static_cast<size_t>(row_bytes));
    else for(uint32_t x=0;x<width_;++x) {
      uint32_t pixel;
      memcpy(&pixel,source_row+static_cast<size_t>(x)*4,sizeof pixel);
      target[x]=(pixel&0xff00ff00u)|((pixel&0x00ff0000u)>>16)|((pixel&0x000000ffu)<<16);
    }
    // Do not form a pointer past a short final source row.
    if(y+1<height_) source_row+=stride_bytes;
  }
  Status result=present(buffer,damage);
  if(result==Status::Ok && submitted) *submitted=buffer;
  return result;
}
void Window::discard_pending() {
  unmap_pixels(pending_pixels_);
  pending_generation_=pending_width_=pending_height_=0;
}
Status Window::request_bounds(int32_t x,int32_t y,uint32_t width,uint32_t height) {
  if(!id_ || !width || !height) return Status::InvalidArgs;
  if(!supports(wm::kFeatureBounds)) return Status::NotSupported;
  if(pending_generation_) return Status::WouldBlock;
  if(width>display_.work.width) width=display_.work.width;
  uint32_t maximum_height=display_.work.height-display_.title_height;
  if(height>maximum_height) height=maximum_height;
  if(width==width_ && height==height_) return set_position(x,y);
  if(generation_==UINT32_MAX) return Status::NoSpace;
  Handle give[2]{};
  Status status=allocate_pixels(width,height,pending_pixels_,give);
  if(status!=Status::Ok) return status;
  uint32_t next_generation=generation_+1;
  wm::Configure request{wm::kConfigure,id_,next_generation,width,height,x,y,0};
  status=channel_write(channel_,&request,sizeof request,give,2);
  if(status!=Status::Ok) { close_handles(give,2); discard_pending(); return status; }
  pending_generation_=next_generation; pending_width_=width; pending_height_=height;
  return Status::Ok;
}
Status Window::set_position(int32_t x,int32_t y) {
  if(!id_) return Status::InvalidArgs;
  if(!supports(wm::kFeatureBounds)) return Status::NotSupported;
  wm::SetPosition request{wm::kSetPosition,id_,x,y};
  return channel_write(channel_,&request,sizeof request,nullptr,0);
}
Status Window::set_title(const char* title) {
  if(!id_ || !title) return Status::InvalidArgs;
  if(!supports(wm::kFeatureTitle)) return Status::NotSupported;
  wm::SetTitle request{wm::kSetTitle,id_,{}};
  strncpy(request.title,title,sizeof request.title-1);
  return channel_write(channel_,&request,sizeof request,nullptr,0);
}
Status Window::set_visible(bool visible,bool activate_window) {
  if(!id_) return Status::InvalidArgs;
  if(!supports(wm::kFeatureVisibility)) return Status::NotSupported;
  wm::SetVisibility request{wm::kSetVisibility,id_,visible ? 1u : 0u,activate_window ? 1u : 0u};
  return channel_write(channel_,&request,sizeof request,nullptr,0);
}
Status Window::activate(bool active) {
  if(!id_) return Status::InvalidArgs;
  if(!supports(wm::kFeatureActivation)) return Status::NotSupported;
  wm::Activate request{wm::kActivate,id_,active ? 1u : 0u};
  return channel_write(channel_,&request,sizeof request,nullptr,0);
}
Status Window::set_capture(bool captured) {
  if(!id_) return Status::InvalidArgs;
  if(!supports(wm::kFeatureCapture)) return Status::NotSupported;
  wm::SetCapture request{wm::kSetCapture,id_,captured ? 1u : 0u};
  return channel_write(channel_,&request,sizeof request,nullptr,0);
}
Status Window::wait(wm::WindowEvent* event, uint64_t deadline) {
  if (event == nullptr || id_ == 0) return Status::InvalidArgs;
  wm::WindowEventV3 managed{};
  Handle hs[4]{};
  auto r = channel_read(channel_, &managed, sizeof managed, hs, 4, deadline);
  if (!r.ok()) return r.status;
  uint32_t nh = static_cast<uint32_t>(r.value); close_handles(hs, nh);
  size_t bytes=protocol_==wm::kManagedVersion ? sizeof managed : sizeof *event;
  uint32_t maximum=protocol_==wm::kManagedVersion ? wm::kCaptureChanged : wm::kCloseRequest;
  if ((r.value >> 32) != bytes || nh != 0 || managed.base.window != id_ ||
      managed.base.type<wm::kFrameDone || managed.base.type>maximum) return Status::InvalidArgs;
  if(protocol_==wm::kManagedVersion) {
    if(managed.base.type==wm::kConfigured) {
      if(!pending_generation_ || managed.generation!=pending_generation_ || managed.base.value>1)
        return Status::InvalidArgs;
      if(!managed.base.value) {
        if(managed.width!=pending_width_ || managed.height!=pending_height_ ||
           managed.x<display_.work.x || int64_t{managed.y}<int64_t{display_.work.y}+display_.title_height ||
           int64_t{managed.x}+managed.width>int64_t{display_.work.x}+display_.work.width ||
           int64_t{managed.y}+managed.height>int64_t{display_.work.y}+display_.work.height)
          return Status::InvalidArgs;
        unmap_pixels(pixels_);
        for(unsigned i=0;i<2;++i) { pixels_[i]=pending_pixels_[i]; pending_pixels_[i]=nullptr; }
        x_=managed.x; y_=managed.y; width_=managed.width; height_=managed.height;
        generation_=pending_generation_; owned_[0]=true; owned_[1]=false;
      }
      discard_pending();
    } else {
      if(managed.generation!=generation_ || managed.width!=width_ || managed.height!=height_)
        return Status::InvalidArgs;
      if(managed.base.type==wm::kBoundsChanged) { x_=managed.x; y_=managed.y; }
    }
  }
  *event=managed.base;
  if (event->type == wm::kFrameDone) {
    if (event->value > 1) return Status::InvalidArgs;
    owned_[event->value] = true;
  } else if(event->type==wm::kFocus) {
    if(event->value>1) return Status::InvalidArgs;
    active_=event->value!=0;
  } else if(event->type==wm::kVisibilityChanged) {
    if(event->value>1) return Status::InvalidArgs;
    visible_=event->value!=0;
  } else if(event->type==wm::kCaptureChanged) {
    if(event->value>1) return Status::InvalidArgs;
    captured_=event->value!=0;
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
