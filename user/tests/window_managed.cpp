#include <charm/window.hpp>
#include <charm/handle.hpp>
#include <charm/libc.h>
#include <stdio.h>

#define VERIFY(expression) do { if(!(expression)) { printf("managed window: failed line %d\n",__LINE__); return 1; } } while(0)
namespace {
template<class Predicate>
bool Await(charm::gui::Window& window,Predicate predicate) {
  uint64_t until=charm::now_ns()+5000000000ull;
  while(!predicate()) {
    abi::wm::WindowEvent event{};
    auto status=window.wait(&event,until);
    if(status==charm::Status::Interrupted) continue;
    if(status!=charm::Status::Ok) return false;
  }
  return true;
}
bool Paint(charm::gui::Window& window,uint32_t rgb) {
  unsigned buffer=window.pixels(0) ? 0 : 1;
  auto* pixels=window.pixels(buffer);
  if(!pixels) return false;
  if(window.format()==abi::syscall::kDisplayRgbx)
    rgb=(rgb&0x00ff00u)|((rgb&0xff0000u)>>16)|((rgb&0x0000ffu)<<16);
  for(size_t i=0;i<size_t{window.width()}*window.height();++i) pixels[i]=rgb;
  return window.present(buffer,{0,0,static_cast<int32_t>(window.width()),static_cast<int32_t>(window.height())})==charm::Status::Ok;
}
bool RejectConfiguration(charm::gui::Window& window,bool undersized) {
  uint32_t width=window.width()+7,height=window.height()+5;
  uint64_t bytes=(uint64_t{width}*height*4+4095)&~uint64_t{4095};
  auto first=charm::vmo_create(bytes),second=charm::vmo_create(undersized ? 4096 : bytes);
  auto first_owner=charm::UniqueHandle::adopt(first.ok() ? static_cast<charm::Handle>(first.value) : 0);
  auto second_owner=charm::UniqueHandle::adopt(second.ok() ? static_cast<charm::Handle>(second.value) : 0);
  if(!first_owner || !second_owner) return false;
  auto a=charm::handle_duplicate(first_owner.get(),charm::rights::kRead|charm::rights::kMap|charm::rights::kTransfer);
  auto b=charm::handle_duplicate(second_owner.get(),charm::rights::kRead|charm::rights::kTransfer|
                                (undersized ? charm::rights::kMap : 0));
  auto give_a=charm::UniqueHandle::adopt(a.ok() ? static_cast<charm::Handle>(a.value) : 0);
  auto give_b=charm::UniqueHandle::adopt(b.ok() ? static_cast<charm::Handle>(b.value) : 0);
  if(!give_a || !give_b) return false;
  charm::Handle give[]={give_a.get(),give_b.get()};
  abi::wm::Configure request{abi::wm::kConfigure,window.id(),window.generation()+1,
                             width,height,window.x(),window.y(),0};
  if(charm::channel_write(window.event_handle(),&request,sizeof request,give,2)!=charm::Status::Ok) return false;
  (void)give_a.release(); (void)give_b.release();
  abi::wm::WindowEventV3 reply{};
  charm::Handle received[4]{};
  auto result=charm::channel_read(window.event_handle(),&reply,sizeof reply,received,4,charm::now_ns()+5000000000ull);
  uint32_t count=result.ok() ? static_cast<uint32_t>(result.value) : 0;
  for(uint32_t i=0;i<count;++i) (void)charm::handle_close(received[i]);
  bool valid=result.ok() && !count && (result.value>>32)==sizeof reply &&
      reply.base.type==abi::wm::kConfigured && reply.base.window==window.id() &&
      reply.base.value==1 && reply.generation==request.generation &&
      reply.width==window.width() && reply.height==window.height() &&
      reply.x==window.x() && reply.y==window.y();
  if(!valid) printf("managed reject: short=%u status=%u bytes=%u handles=%u type=%u value=%u generation=%u expected=%u\n",
      undersized,static_cast<unsigned>(result.status),static_cast<unsigned>(result.value>>32),count,
      reply.base.type,reply.base.value,reply.generation,request.generation);
  return valid;
}
// Stall one consumer until the compositor's bounded reply queue fills.
// Other windows must survive, and queued messages must remain in order.
bool Backpressure() {
  charm::gui::Window stalled;
  if(stalled.create_managed("Slow consumer",64,32,
      abi::wm::kCreateInvisible|abi::wm::kCreateInactive)!=charm::Status::Ok) return false;
  unsigned sent=0;
  uint64_t deadline=charm::now_ns()+5000000000ull;
  bool disconnected=false;
  while(sent<1024 && charm::now_ns()<deadline) {
    auto status=stalled.set_position(static_cast<int32_t>(sent%200),120);
    if(status==charm::Status::PeerClosed) { disconnected=true; break; }
    if(status==charm::Status::WouldBlock) { charm::yield(); continue; }
    if(status!=charm::Status::Ok) return false;
    ++sent;
  }
  if(!disconnected) {
    // Let the compositor drain requests without draining its replies.
    charm::sleep_until(charm::now_ns()+100000000ull);
  }
  unsigned received=0;
  for(;;) {
    abi::wm::WindowEvent event{};
    auto status=stalled.wait(&event,deadline);
    if(status==charm::Status::PeerClosed) break;
    if(status==charm::Status::Interrupted) continue;
    if(status!=charm::Status::Ok || event.type!=abi::wm::kBoundsChanged ||
       stalled.x()!=static_cast<int32_t>(received%200) || stalled.y()!=120) return false;
    ++received;
  }
  return received>0 && received<sent;
}
}

extern "C" int main() {
  charm::gui::Window legacy;
  VERIFY(legacy.create("Legacy compatibility",64,32)==charm::Status::Ok);
  VERIFY(legacy.protocol_version()==2 && legacy.set_title("unsupported")==charm::Status::NotSupported &&
         legacy.request_bounds(0,0,80,40)==charm::Status::NotSupported &&
         legacy.set_visible(false)==charm::Status::NotSupported && legacy.activate()==charm::Status::NotSupported);
  VERIFY(Paint(legacy,0x234e70) && Await(legacy,[&] { return legacy.pixels(1)!=nullptr; }));
  legacy.close();

  charm::gui::Window window;
  VERIFY(window.create_managed("Managed native window",2000,1200,
         abi::wm::kCreateInvisible|abi::wm::kCreateInactive)==charm::Status::Ok);
  VERIFY(window.protocol_version()==3 && window.generation()==1 && !window.visible() && !window.active());
  auto display=window.display();
  VERIFY((display.features&15)==15 && display.title_height && display.work.height>display.title_height);
  VERIFY(window.width()==display.work.width && window.height()==display.work.height-display.title_height);
  VERIFY(window.set_title("Managed native window — resize verified")==charm::Status::Ok);
  VERIFY(window.set_visible(true,false)==charm::Status::Ok &&
         Await(window,[&] { return window.visible(); }) && !window.active());
  VERIFY(window.activate()==charm::Status::Ok && Await(window,[&] { return window.active(); }));
  VERIFY(Paint(window,0x234e70));
  VERIFY(window.request_bounds(40,120,360,180)==charm::Status::Ok && window.configuration_pending());
  VERIFY(window.request_bounds(40,120,361,181)==charm::Status::WouldBlock);
  VERIFY(!window.pixels(0) && !window.pixels(1));
  VERIFY(window.present(0,{0,0,10,10})==charm::Status::WouldBlock);
  VERIFY(Await(window,[&] { return !window.configuration_pending(); }));
  VERIFY(window.generation()==2 && window.width()==360 && window.height()==180 && window.x()==40 && window.y()==120);
  VERIFY(window.pixels(0) && !window.pixels(1));
  VERIFY(Paint(window,0x24a148) && Await(window,[&] { return window.pixels(1)!=nullptr; }));

  for(unsigned iteration=0;iteration<8;++iteration) {
    uint32_t generation=window.generation();
    uint32_t width=320+iteration*5,height=160+iteration*3;
    VERIFY(window.request_bounds(100,160,width,height)==charm::Status::Ok);
    VERIFY(Await(window,[&] { return !window.configuration_pending(); }));
    VERIFY(window.generation()==generation+1 && window.width()==width && window.height()==height);
    VERIFY(Paint(window,0x24a148) && Await(window,[&] { return window.pixels(1)!=nullptr; }));
  }
  VERIFY(RejectConfiguration(window,true) && RejectConfiguration(window,false));
  VERIFY(window.request_bounds(120,180,400,220)==charm::Status::Ok &&
         Await(window,[&] { return !window.configuration_pending(); }) && window.width()==400 && window.height()==220);
  VERIFY(Paint(window,0x24a148) && Await(window,[&] { return window.pixels(1)!=nullptr; }));
  VERIFY(window.set_position(INT32_MAX,INT32_MIN)==charm::Status::Ok);
  VERIFY(Await(window,[&] { return window.x()==static_cast<int32_t>(display.work.width-window.width()) &&
      window.y()==display.work.y+static_cast<int32_t>(display.title_height); }));
  VERIFY(window.set_visible(false)==charm::Status::Ok &&
         Await(window,[&] { return !window.visible() && !window.active(); }));
  VERIFY(window.set_visible(true)==charm::Status::Ok &&
         Await(window,[&] { return window.visible() && window.active(); }));
  VERIFY(window.activate(false)==charm::Status::Ok && Await(window,[&] { return !window.active(); }));
  VERIFY(window.activate()==charm::Status::Ok && Await(window,[&] { return window.active(); }));

  charm::gui::Window rogue;
  VERIFY(rogue.create_managed("Invalid generation",64,32,
         abi::wm::kCreateInvisible|abi::wm::kCreateInactive)==charm::Status::Ok);
  abi::wm::PresentV3 invalid{{abi::wm::kPresent,rogue.id(),0,0,0,64,32},rogue.generation()+1};
  VERIFY(charm::channel_write(rogue.event_handle(),&invalid,sizeof invalid,nullptr,0)==charm::Status::Ok);
  abi::wm::WindowEvent closed{};
  VERIFY(rogue.wait(&closed,charm::now_ns()+5000000000ull)==charm::Status::PeerClosed);
  rogue.close();
  VERIFY(Backpressure());
  VERIFY(window.set_position(120,180)==charm::Status::Ok &&
         Await(window,[&] { return window.x()==120 && window.y()==180; }));
  printf("managed window: resize generations, rollback, title, visibility and focus ok\n");
  bool submitted=false,rendered=false;
  uint64_t deadline=charm::now_ns()+30000000000ull;
  for(;;) {
    abi::wm::WindowEvent event{};
    auto status=window.wait(&event,deadline);
    if(status==charm::Status::Interrupted) continue;
    VERIFY(status==charm::Status::Ok);
    if(event.type==abi::wm::kInput) {
      abi::syscall::InputEvent input{};
      while(window.next_input(&input)) {
        if(input.type!=abi::syscall::kInputKey || input.value!=1) continue;
        if(input.code==0x04 && !submitted) { VERIFY(Paint(window,0xf5c518)); submitted=true; }
        if(input.code==0x29) {
          VERIFY(rendered);
          printf("managed window: closed\n");
          return 0;
        }
      }
    }
    if(event.type==abi::wm::kFrameDone && submitted && !rendered) {
      rendered=true;
      printf("managed window: input after resize rendered\n");
    }
  }
}
