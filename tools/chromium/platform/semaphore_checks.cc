#include <atomic>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <charm/sys.hpp>

namespace {
sem_t* signal_semaphore=nullptr;
volatile sig_atomic_t signal_failure=0;
void PostFromSignal(int) {
  if(!signal_semaphore || sem_post(signal_semaphore)) signal_failure=1;
}
bool Deadline(timespec* output,unsigned milliseconds) {
  if(clock_gettime(CLOCK_REALTIME,output)) return false;
  output->tv_sec+=milliseconds/1000;
  output->tv_nsec+=static_cast<long>(milliseconds%1000)*1000000;
  if(output->tv_nsec>=1000000000) { ++output->tv_sec; output->tv_nsec-=1000000000; }
  return true;
}
bool WaitBefore(sem_t* semaphore,const timespec& until) {
  while(sem_timedwait(semaphore,&until)) {
    if(errno!=EINTR) return false;
  }
  return true;
}
}

int TestNativeSemaphores() {
  sem_t semaphore{};
  int value=-1;
  if(sem_wait(&semaphore)!=-1 || errno!=EINVAL) return 1;
  sem_t original;
  memset(&semaphore,0x55,sizeof semaphore);
  original=semaphore;
  if(sem_init(&semaphore,1,0)!=-1 || errno!=ENOTSUP ||
     memcmp(&semaphore,&original,sizeof original)) return 2;
  if(sem_init(&semaphore,0,static_cast<unsigned>(SEM_VALUE_MAX)+1)!=-1 || errno!=EINVAL ||
     memcmp(&semaphore,&original,sizeof original)) return 3;
  if(sem_init(&semaphore,0,SEM_VALUE_MAX) || sem_post(&semaphore)!=-1 || errno!=EOVERFLOW ||
     sem_getvalue(&semaphore,&value) || value!=SEM_VALUE_MAX || sem_destroy(&semaphore)) return 4;
  if(sem_init(&semaphore,0,1)) return 5;
  timespec invalid{0,1000000000};
  errno=EDOM;
  if(sem_timedwait(&semaphore,&invalid) || errno!=EDOM) return 6;
  if(sem_trywait(&semaphore)!=-1 || errno!=EAGAIN) return 7;
  if(sem_timedwait(&semaphore,&invalid)!=-1 || errno!=EINVAL) return 8;
  timespec expired{-1,0};
  if(sem_timedwait(&semaphore,&expired)!=-1 || errno!=ETIMEDOUT) return 9;
  timespec deadline{};
  if(!Deadline(&deadline,15) || sem_timedwait(&semaphore,&deadline)!=-1 || errno!=ETIMEDOUT ||
     sem_getvalue(&semaphore,&value) || value) return 10;

  auto old_handler=signal(SIGALRM,PostFromSignal);
  if(old_handler==SIG_ERR) return 11;
  signal_semaphore=&semaphore;
  auto alarm_status=charm::syscall1(abi::syscall::Number::SignalAlarm,
                                   charm::now_ns()+10000000).status;
  bool received=Deadline(&deadline,2000) && WaitBefore(&semaphore,deadline);
  (void)alarm(0);
  (void)signal(SIGALRM,old_handler);
  signal_semaphore=nullptr;
  if(!received || alarm_status!=charm::Status::Ok || signal_failure || sem_destroy(&semaphore)) return 12;
  if(sem_post(&semaphore)!=-1 || errno!=EINVAL) return 13;

  sem_t produced{},consumed{};
  if(sem_init(&produced,0,0) || sem_init(&consumed,0,0)) return 14;
  int payload=0;
  std::atomic<bool> failure{false};
  std::thread consumer([&] {
    for(int expected=1;expected<=1000;++expected) {
      timespec until{};
      if(!Deadline(&until,2000) || !WaitBefore(&produced,until)) { failure=true; return; }
      if(payload!=expected) failure=true;
      if(sem_post(&consumed)) { failure=true; return; }
    }
  });
  for(int next=1;next<=1000;++next) {
    payload=next;
    if(sem_post(&produced) || !Deadline(&deadline,2000) || !WaitBefore(&consumed,deadline)) {
      failure=true;
      break;
    }
  }
  consumer.join();
  if(failure.load() || sem_destroy(&produced) || sem_destroy(&consumed)) return 15;

  if(sem_init(&semaphore,0,0)) return 16;
  std::atomic<unsigned> tokens{0};
  std::thread waiters[8];
  for(auto& waiter:waiters) waiter=std::thread([&] {
    timespec until{};
    if(!Deadline(&until,3000)) { failure=true; return; }
    for(unsigned i=0;i<100;++i) {
      if(!WaitBefore(&semaphore,until)) { failure=true; return; }
      ++tokens;
    }
  });
  for(unsigned i=0;i<800;++i) if(sem_post(&semaphore)) failure=true;
  for(auto& waiter:waiters) waiter.join();
  if(failure.load() || tokens.load()!=800 || sem_getvalue(&semaphore,&value) || value ||
     sem_destroy(&semaphore)) return 17;
  return 0;
}
