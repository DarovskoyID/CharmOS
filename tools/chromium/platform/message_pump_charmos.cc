#include "build/charmos/message_pump_charmos.h"
#include "base/check.h"
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>

namespace base {
MessagePumpCharmOS::FdWatchController::FdWatchController(const Location& location)
    :FdWatchControllerInterface(location) {}
MessagePumpCharmOS::FdWatchController::~FdWatchController() { StopWatchingFileDescriptor(); }
bool MessagePumpCharmOS::FdWatchController::StopWatchingFileDescriptor() {
  if (watch_ && watch_->pump) watch_->pump->Stop(watch_);
  watch_.reset();
  return true;
}
MessagePumpCharmOS::MessagePumpCharmOS():owner_(gettid()) {
  int endpoints[2];
  CHECK(pipe2(endpoints,O_NONBLOCK|O_CLOEXEC)==0);
  wake_read_=endpoints[0]; wake_write_=endpoints[1];
}
MessagePumpCharmOS::~MessagePumpCharmOS() {
  CHECK(gettid()==owner_ && !state_);
  for (auto& watch:watches_) { watch->pump=nullptr; watch->watcher=nullptr; watch->armed=false; }
  CHECK(close(wake_read_)==0);
  CHECK(close(wake_write_)==0);
}
void MessagePumpCharmOS::Stop(const std::shared_ptr<Watch>& watch) {
  CHECK(gettid()==owner_);
  watch->armed=false; watch->watcher=nullptr; watch->pump=nullptr;
  std::erase(watches_,watch);
}
bool MessagePumpCharmOS::WatchFileDescriptor(int fd,bool persistent,int mode,
                                            FdWatchController* controller,FdWatcher* watcher) {
  CHECK(gettid()==owner_);
  if (!controller) return false;
  if (fd<0 || !watcher || !mode || (mode&~WATCH_READ_WRITE) || fcntl(fd,F_GETFD)<0) {
    controller->StopWatchingFileDescriptor(); return false;
  }
  auto previous=controller->watch_;
  std::vector<int> distinct;
  for (const auto& watch:watches_) {
    if (watch->armed && std::find(distinct.begin(),distinct.end(),watch->fd)==distinct.end())
      distinct.push_back(watch->fd);
  }
  if (distinct.size()>=63 && std::find(distinct.begin(),distinct.end(),fd)==distinct.end()) {
    controller->StopWatchingFileDescriptor(); return false;
  }
  if (previous && previous->pump) {
    if (previous->pump!=this || previous->fd!=fd || previous->persistent!=persistent) {
      controller->StopWatchingFileDescriptor(); return false;
    }
    previous->mode|=mode;
    previous->watcher=watcher;
    previous->armed=true;
    return true;
  }
  auto registration=std::make_shared<Watch>(Watch{this,watcher,fd,mode,persistent,true});
  controller->watch_=registration;
  watches_.push_back(std::move(registration));
  return true;
}
void MessagePumpCharmOS::ScheduleWork() {
  char wake=1;
  ssize_t result;
  do { result=write(wake_write_,&wake,1); } while (result<0 && errno==EINTR);
  CHECK(result==1 || (result<0 && errno==EAGAIN));
}
void MessagePumpCharmOS::ScheduleDelayedWork(const Delegate::NextWorkInfo&) { ScheduleWork(); }
void MessagePumpCharmOS::Quit() {
  CHECK(gettid()==owner_);
  if (state_) state_->quit=true;
}
bool MessagePumpCharmOS::PollAndDispatch(int timeout,RunState* state) {
  auto snapshot=watches_;
  std::vector<pollfd> descriptors{{wake_read_,POLLIN,0}};
  for (const auto& watch:snapshot) {
    if (!watch->armed) continue;
    auto found=std::find_if(descriptors.begin()+1,descriptors.end(),
                            [&](const pollfd& entry) { return entry.fd==watch->fd; });
    short wanted=static_cast<short>(((watch->mode&WATCH_READ) ? POLLIN : 0)|
                                   ((watch->mode&WATCH_WRITE) ? POLLOUT : 0));
    if (found==descriptors.end()) descriptors.push_back({watch->fd,wanted,0});
    else found->events|=wanted;
  }
  int ready=poll(descriptors.data(),descriptors.size(),timeout);
  if (ready<0 && errno==EINTR) return false;
  CHECK(ready>=0);
  if (!ready) return false;
  state->delegate->BeginNativeWorkBeforeDoWork();
  if (descriptors[0].revents) {
    CHECK(!(descriptors[0].revents&(POLLERR|POLLHUP|POLLNVAL)));
    char bytes[64];
    for (;;) {
      ssize_t count=read(wake_read_,bytes,sizeof bytes);
      if (count>0) continue;
      if (count<0 && errno==EINTR) continue;
      CHECK(count<0 && errno==EAGAIN);
      break;
    }
  }
  for (const auto& watch:snapshot) {
    if (state->quit) break;
    if (!watch->armed || watch->pump!=this || !watch->watcher) continue;
    auto entry=std::find_if(descriptors.begin()+1,descriptors.end(),
                           [&](const pollfd& item) { return item.fd==watch->fd; });
    if (entry==descriptors.end()) continue;
    bool can_read=(watch->mode&WATCH_READ) && (entry->revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL));
    bool can_write=(watch->mode&WATCH_WRITE) && (entry->revents&(POLLOUT|POLLHUP|POLLERR|POLLNVAL));
    if (!can_read && !can_write) continue;
    if (!watch->persistent) watch->armed=false;
    auto work_item=state->delegate->BeginWorkItem();
    if (can_read) watch->watcher->OnFileCanReadWithoutBlocking(watch->fd);
    if (!state->quit && can_write && watch->watcher && (!can_read || (watch->persistent && watch->armed)))
      watch->watcher->OnFileCanWriteWithoutBlocking(watch->fd);
  }
  return true;
}
void MessagePumpCharmOS::Run(Delegate* delegate) {
  CHECK(gettid()==owner_ && delegate);
  RunState local{delegate};
  RunState* previous=state_;
  state_=&local;
  while (!local.quit) {
    bool native=PollAndDispatch(0,&local);
    if (local.quit) break;
    auto next=delegate->DoWork();
    if (local.quit) break;
    if (native || next.is_immediate()) continue;
    delegate->DoIdleWork();
    if (local.quit) break;
    delegate->BeforeWait();
    if (local.quit) break;
    int timeout=-1;
    if (!next.delayed_run_time.is_max()) {
      int64_t delay=(next.delayed_run_time-TimeTicks::Now()).InMillisecondsRoundedUp();
      timeout=static_cast<int>(std::clamp<int64_t>(delay,0,INT_MAX));
    }
    (void)PollAndDispatch(timeout,&local);
  }
  state_=previous;
}
}  // namespace base
