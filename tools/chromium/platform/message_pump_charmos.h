#ifndef CHARMOS_MESSAGE_PUMP_H_
#define CHARMOS_MESSAGE_PUMP_H_
#include "base/message_loop/message_pump.h"
#include "base/message_loop/watchable_io_message_pump_posix.h"
#include <memory>
#include <vector>
#include <unistd.h>

namespace base {
// Native readiness uses cancellable kernel ports through libc poll, without
// epoll, eventfd, Linux signals or a periodic idle wakeup.
class BASE_EXPORT MessagePumpCharmOS final : public MessagePump,
                                            public WatchableIOMessagePumpPosix {
  struct Watch;
 public:
  class FdWatchController final : public FdWatchControllerInterface {
   public:
    explicit FdWatchController(const Location& location);
    ~FdWatchController() override;
    bool StopWatchingFileDescriptor() override;
   private:
    friend class MessagePumpCharmOS;
    std::shared_ptr<Watch> watch_;
  };
  MessagePumpCharmOS();
  ~MessagePumpCharmOS() override;
  bool WatchFileDescriptor(int fd,bool persistent,int mode,FdWatchController* controller,FdWatcher* watcher);
  void Run(Delegate* delegate) override;
  void Quit() override;
  void ScheduleWork() override;
  void ScheduleDelayedWork(const Delegate::NextWorkInfo& next_work_info) override;
 private:
  struct RunState { Delegate* delegate; bool quit=false; };
  struct Watch {
    MessagePumpCharmOS* pump;
    FdWatcher* watcher;
    int fd, mode;
    bool persistent, armed;
  };
  void Stop(const std::shared_ptr<Watch>& watch);
  bool PollAndDispatch(int timeout,RunState* state);
  pid_t owner_;
  int wake_read_=-1, wake_write_=-1;
  RunState* state_=nullptr;
  std::vector<std::shared_ptr<Watch>> watches_;
};
}  // namespace base
#endif
