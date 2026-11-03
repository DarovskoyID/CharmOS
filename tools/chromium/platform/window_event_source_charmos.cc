#include "build/charmos/window_event_source_charmos.h"

#include "base/check.h"
#include "base/task/current_thread.h"
#include <charm/libc.h>

namespace ui {
WindowEventSourceCharmOS::WindowEventSourceCharmOS()=default;
WindowEventSourceCharmOS::~WindowEventSourceCharmOS() { Stop(); }
bool WindowEventSourceCharmOS::Start(charm::gui::Window* window,Callback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
  if(!window || !window->id() || callback.is_null() || !base::CurrentUIThread::IsSet())
    return false;
  readiness_.reset(charm_channel_watch_fd(window->event_handle(),1));
  if(!readiness_.is_valid()) return false;
  window_=window;
  callback_=std::move(callback);
  if(!base::CurrentUIThread::Get()->WatchFileDescriptor(readiness_.get(),true,
      base::MessagePumpForUI::WATCH_READ,&controller_,this)) {
    Stop();
    return false;
  }
  return true;
}
void WindowEventSourceCharmOS::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  controller_.StopWatchingFileDescriptor();
  readiness_.reset();
  callback_.Reset();
  window_=nullptr;
}
void WindowEventSourceCharmOS::Fail(charm::Status status) {
  auto callback=callback_;
  Stop();
  if(!callback.is_null()) callback.Run(status,abi::wm::WindowEvent{});
}
void WindowEventSourceCharmOS::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if(fd!=readiness_.get() || !window_) return;
  for(unsigned count=0;count<64;++count) {
    abi::wm::WindowEvent event{};
    auto status=window_->wait(&event,charm::now_ns());
    if(status==charm::Status::TimedOut || status==charm::Status::WouldBlock) return;
    if(status==charm::Status::Interrupted) continue;
    if(status!=charm::Status::Ok) { Fail(status); return; }
    auto weak=weak_factory_.GetWeakPtr();
    auto callback=callback_;
    callback.Run(status,event);
    if(!weak) return;
  }
}
void WindowEventSourceCharmOS::OnFileCanWriteWithoutBlocking(int) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Fail(charm::Status::InvalidArgs);
}
}  // namespace ui
