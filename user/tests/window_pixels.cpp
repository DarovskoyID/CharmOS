#include <charm/window.hpp>
#include <charm/libc.h>
#include <charm/handle.hpp>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VERIFY(condition) do { if(!(condition)) { printf("pixel api: failed line %d\n",__LINE__); return 1; } } while(0)
namespace {
bool RejectedBuffers(bool undersized) {
  charm::Handle pair[2]{};
  if(charm::channel_create(pair)!=charm::Status::Ok) return false;
  auto connection=charm::UniqueHandle::adopt(pair[0]);
  auto server=charm::UniqueHandle::adopt(pair[1]);
  abi::wm::Helo hello{abi::wm::kHelo,abi::wm::kVersion};
  if(charm::channel_write(charm_wm(),&hello,sizeof hello,&pair[1],1)!=charm::Status::Ok) return false;
  (void)server.release();
  uint64_t deadline=charm::now_ns()+5000000000ull;
  abi::wm::Welcome welcome{};
  charm::Handle received[4]{};
  auto read=charm::channel_read(connection.get(),&welcome,sizeof welcome,received,4,deadline);
  unsigned count=read.ok() ? static_cast<unsigned>(read.value) : 0;
  for(unsigned i=0;i<count;++i) (void)charm::handle_close(received[i]);
  if(!read.ok() || count || (read.value>>32)!=sizeof welcome || welcome.type!=abi::wm::kWelcome)
    return false;
  auto first=charm::vmo_create(96*64*4);
  auto first_owner=charm::UniqueHandle::adopt(first.ok() ? static_cast<charm::Handle>(first.value) : 0);
  auto second=charm::vmo_create(undersized ? 4096 : 96*64*4);
  auto second_owner=charm::UniqueHandle::adopt(second.ok() ? static_cast<charm::Handle>(second.value) : 0);
  if(!first_owner || !second_owner) return false;
  auto duplicate=charm::handle_duplicate(second_owner.get(),charm::rights::kRead|charm::rights::kTransfer);
  auto restricted=charm::UniqueHandle::adopt(duplicate.ok() ? static_cast<charm::Handle>(duplicate.value) : 0);
  if(!restricted) return false;
  charm::Handle gifts[]={first_owner.get(),undersized ? second_owner.get() : restricted.get()};
  abi::wm::Create create{abi::wm::kCreate,96,64,"Invalid buffer test"};
  if(charm::channel_write(connection.get(),&create,sizeof create,gifts,2)!=charm::Status::Ok) return false;
  (void)first_owner.release();
  if(undersized) (void)second_owner.release();
  else (void)restricted.release();
  abi::wm::Created reply{};
  read=charm::channel_read(connection.get(),&reply,sizeof reply,received,4,deadline);
  count=read.ok() ? static_cast<unsigned>(read.value) : 0;
  for(unsigned i=0;i<count;++i) (void)charm::handle_close(received[i]);
  return read.ok() && !count && (read.value>>32)==sizeof reply &&
      reply.type==abi::wm::kCreated && !reply.window;
}
}
extern "C" int main() {
  VERIFY(RejectedBuffers(true) && RejectedBuffers(false));
  printf("pixel api: compositor rejected short and unmappable buffers\n");
  constexpr uint32_t width=96,height=64,stride=400;
  constexpr size_t source_size=(height-1)*stride+width*4;
  charm::gui::Window window;
  VERIFY(window.create("Pixel API test",width,height)==charm::Status::Ok);
  auto* first=window.pixels(0);
  auto* second=window.pixels(1);
  VERIFY(first && !second);
  first[0]=0xaabbccddu;
  auto* source=static_cast<unsigned char*>(malloc(source_size));
  VERIFY(source);
  memset(source,0x7f,source_size);
  uint32_t source_pixel=0x00112233u;
  for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x)
    memcpy(source+static_cast<size_t>(y)*stride+x*4,&source_pixel,sizeof source_pixel);
  uint32_t source_format=window.format()==abi::syscall::kDisplayBgrx ?
      abi::syscall::kDisplayRgbx : abi::syscall::kDisplayBgrx;
  charm::gui::Damage damage{0,0,static_cast<int32_t>(width),static_cast<int32_t>(height)};
  uint32_t submitted=99;
  VERIFY(window.present_pixels(source,source_size,stride-20,source_format,damage,&submitted)==charm::Status::InvalidArgs);
  VERIFY(window.present_pixels(source,source_size-1,stride,source_format,damage,&submitted)==charm::Status::InvalidArgs);
  VERIFY(window.present_pixels(source,source_size,stride,99,damage,&submitted)==charm::Status::InvalidArgs);
  VERIFY(window.present_pixels(first,width*height*4,width*4,source_format,damage,&submitted)==charm::Status::InvalidArgs);
  VERIFY(submitted==99 && first[0]==0xaabbccddu);
  VERIFY(window.present_pixels(source,source_size,stride,source_format,damage,&submitted)==charm::Status::Ok && submitted==0);
  VERIFY(!window.pixels(0) && !window.pixels(1));
  VERIFY(first[0]==0x00332211u && first[width]==0x00332211u && first[width*height-1]==0x00332211u);
  submitted=99;
  VERIFY(window.present_pixels(source,source_size,stride,source_format,damage,&submitted)==charm::Status::WouldBlock && submitted==99);
  int readiness=charm_channel_watch_fd(window.event_handle(),1);
  VERIFY(readiness>=0 && (fcntl(readiness,F_GETFD)&FD_CLOEXEC));
  pollfd ready{readiness,POLLIN,0};
  uint64_t deadline=charm::now_ns()+5000000000;
  while(!window.pixels(0) && !window.pixels(1)) {
    VERIFY(charm::now_ns()<deadline && poll(&ready,1,1000)>=0);
    abi::wm::WindowEvent event{};
    auto result=window.wait(&event,charm::now_ns());
    if(result==charm::Status::TimedOut || result==charm::Status::WouldBlock) continue;
    VERIFY(result==charm::Status::Ok);
  }
  second=window.pixels(1);
  VERIFY(second && !window.pixels(0));
  VERIFY(window.present_pixels(source,source_size,stride,source_format,damage,&submitted)==charm::Status::Ok && submitted==1);
  VERIFY(second[0]==0x00332211u && !window.pixels(1));
  submitted=99;
  VERIFY(window.present_pixels(source,source_size,stride,source_format,damage,&submitted)==charm::Status::WouldBlock && submitted==99);
  while(!window.pixels(0)) {
    VERIFY(charm::now_ns()<deadline && poll(&ready,1,1000)>=0);
    abi::wm::WindowEvent event{};
    auto result=window.wait(&event,charm::now_ns());
    if(result==charm::Status::TimedOut || result==charm::Status::WouldBlock) continue;
    VERIFY(result==charm::Status::Ok);
  }
  VERIFY(window.present_pixels(source,source_size,stride,source_format,damage,&submitted)==charm::Status::Ok && submitted<2);
  VERIFY(close(readiness)==0);
  window.close();
  free(source);
  printf("pixel api: full frames, format conversion, bounds and readiness ok\n");
  return 0;
}
