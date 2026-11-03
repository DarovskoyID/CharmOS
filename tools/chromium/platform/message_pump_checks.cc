#include "build/charmos/message_pump_charmos.h"
#include "base/location.h"
#include <atomic>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {
using Pump=base::MessagePumpCharmOS;
class Delegate final : public base::MessagePump::Delegate {
 public:
  explicit Delegate(Pump& pump):pump_(pump),deadline_(base::TimeTicks::Now()+base::Seconds(1)) {}
  NextWorkInfo DoWork() override {
    auto now=base::TimeTicks::Now();
    if (ready && ready->load()) pump_.Quit();
    if (now>=deadline_) { timed_out=true; pump_.Quit(); }
    NextWorkInfo next;
    next.delayed_run_time=deadline_;
    next.recent_now=now;
    return next;
  }
  void DoIdleWork() override {}
  void BeforeWait() override {}
  void BeginNativeWorkBeforeDoWork() override {}
  int RunDepth() override { return 1; }
  void OnBeginWorkItem() override { ++begun; }
  void OnEndWorkItem(int) override { ++ended; }
  std::atomic<bool>* ready=nullptr;
  bool timed_out=false;
  unsigned begun=0,ended=0;
 private:
  Pump& pump_;
  base::TimeTicks deadline_;
};
class Watcher final : public Pump::FdWatcher {
 public:
  explicit Watcher(Pump& pump):pump_(pump) {}
  void OnFileCanReadWithoutBlocking(int fd) override {
    char byte=0;
    good=read(fd,&byte,1)==1 && byte=='x';
    ++calls;
    // Destroy both the currently executing controller and another controller
    // already retained by the dispatch snapshot.
    if (self) self->reset();
    if (other) other->reset();
    pump_.Quit();
  }
  void OnFileCanWriteWithoutBlocking(int) override { good=false; pump_.Quit(); }
  std::unique_ptr<Pump::FdWatchController>* self=nullptr;
  std::unique_ptr<Pump::FdWatchController>* other=nullptr;
  bool good=false;
  unsigned calls=0;
 private:
  Pump& pump_;
};
}

int TestNativeMessagePump() {
  Pump pump;
  int pipe_fds[2];
  if (pipe(pipe_fds)) return 20;
  auto first=std::make_unique<Pump::FdWatchController>(FROM_HERE);
  auto second=std::make_unique<Pump::FdWatchController>(FROM_HERE);
  Watcher first_watcher(pump),second_watcher(pump);
  first_watcher.self=&first;
  first_watcher.other=&second;
  if (!pump.WatchFileDescriptor(pipe_fds[0],false,Pump::WATCH_READ,first.get(),&first_watcher) ||
      !pump.WatchFileDescriptor(pipe_fds[0],true,Pump::WATCH_READ,second.get(),&second_watcher)) return 21;
  std::thread writer([&] { usleep(5000); (void)write(pipe_fds[1],"x",1); });
  Delegate delegate(pump);
  pump.Run(&delegate);
  writer.join();
  close(pipe_fds[0]); close(pipe_fds[1]);
  if (delegate.timed_out || !first_watcher.good || first_watcher.calls!=1 ||
      second_watcher.calls || first || second || delegate.begun!=delegate.ended) return 22;

  std::atomic<bool> ready{false};
  Delegate wake_delegate(pump);
  wake_delegate.ready=&ready;
  std::thread producer([&] { usleep(5000); ready.store(true); pump.ScheduleWork(); });
  pump.Run(&wake_delegate);
  producer.join();
  if (wake_delegate.timed_out || !ready.load()) return 23;
  return 0;
}
