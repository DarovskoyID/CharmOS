#include <semaphore.h>
#include <errno.h>
#include <stddef.h>
#include "internal.hpp"

static_assert(sizeof(sem_t)==16 && alignof(sem_t)==4);
static_assert(offsetof(sem_t,__count)==0 && offsetof(sem_t,__waiters)==4);
static_assert(offsetof(sem_t,__magic)==8 && offsetof(sem_t,__reserved)==12);
namespace {
constexpr uint32_t kSemaphoreMagic=0x53454d31;
bool valid(sem_t* semaphore) {
  return semaphore && __atomic_load_n(&semaphore->__magic,__ATOMIC_ACQUIRE)==kSemaphoreMagic;
}
int fail(int error) { errno=error; return -1; }
bool try_take(sem_t* semaphore) {
  uint32_t available=__atomic_load_n(&semaphore->__count,__ATOMIC_RELAXED);
  while(available) {
    if(__atomic_compare_exchange_n(&semaphore->__count,&available,available-1,
                                  true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return true;
  }
  return false;
}
struct Waiting {
  sem_t* semaphore;
  explicit Waiting(sem_t* value):semaphore(value) {
    __atomic_fetch_add(&semaphore->__waiters,1u,__ATOMIC_ACQ_REL);
  }
  ~Waiting() { __atomic_fetch_sub(&semaphore->__waiters,1u,__ATOMIC_ACQ_REL); }
};
int wait_slow(sem_t* semaphore,uint64_t deadline) {
  Waiting waiting(semaphore);
  for(;;) {
    if(try_take(semaphore)) return 0;
    auto status=charm::futex_wait(&semaphore->__count,0,deadline);
    if(status==charm::Status::Ok || status==charm::Status::WouldBlock) continue;
    return fail(libc::errno_from(status));
  }
}
}  // namespace

extern "C" {
int sem_init(sem_t* semaphore,int pshared,unsigned value) {
  if(!semaphore || value>SEM_VALUE_MAX) return fail(EINVAL);
  if(pshared) return fail(ENOTSUP);
  semaphore->__count=value;
  semaphore->__waiters=0;
  semaphore->__reserved=0;
  __atomic_store_n(&semaphore->__magic,kSemaphoreMagic,__ATOMIC_RELEASE);
  return 0;
}
int sem_destroy(sem_t* semaphore) {
  if(!valid(semaphore)) return fail(EINVAL);
  if(__atomic_load_n(&semaphore->__waiters,__ATOMIC_ACQUIRE)) return fail(EBUSY);
  __atomic_store_n(&semaphore->__magic,0u,__ATOMIC_RELEASE);
  return 0;
}
int sem_trywait(sem_t* semaphore) {
  if(!valid(semaphore)) return fail(EINVAL);
  return try_take(semaphore) ? 0 : fail(EAGAIN);
}
int sem_wait(sem_t* semaphore) {
  if(!valid(semaphore)) return fail(EINVAL);
  return try_take(semaphore) ? 0 : wait_slow(semaphore,charm::kForever);
}
int sem_timedwait(sem_t* semaphore,const timespec* absolute) {
  if(!valid(semaphore)) return fail(EINVAL);
  if(try_take(semaphore)) return 0;
  if(!absolute || absolute->tv_nsec<0 || absolute->tv_nsec>=1000000000) return fail(EINVAL);
  if(absolute->tv_sec<0) return fail(ETIMEDOUT);
  uint64_t seconds=static_cast<uint64_t>(absolute->tv_sec);
  if(seconds>(UINT64_MAX-static_cast<uint64_t>(absolute->tv_nsec))/1000000000)
    return fail(EINVAL);
  uint64_t target=seconds*1000000000+static_cast<uint64_t>(absolute->tv_nsec);
  auto real=charm::realtime();
  if(!real.ok()) return fail(libc::errno_from(real.status));
  uint64_t remaining=target>real.value ? target-real.value : 0;
  uint64_t now=charm::now_ns();
  uint64_t deadline=remaining>UINT64_MAX-now ? UINT64_MAX : now+remaining;
  return wait_slow(semaphore,deadline);
}
int sem_post(sem_t* semaphore) {
  if(!valid(semaphore)) return fail(EINVAL);
  uint32_t available=__atomic_load_n(&semaphore->__count,__ATOMIC_RELAXED);
  for(;;) {
    if(available>=SEM_VALUE_MAX) return fail(EOVERFLOW);
    if(__atomic_compare_exchange_n(&semaphore->__count,&available,available+1,
                                  true,__ATOMIC_RELEASE,__ATOMIC_RELAXED)) break;
  }
  (void)charm::futex_wake(&semaphore->__count,1);
  return 0;
}
int sem_getvalue(sem_t* semaphore,int* value) {
  if(!valid(semaphore) || !value) return fail(EINVAL);
  *value=static_cast<int>(__atomic_load_n(&semaphore->__count,__ATOMIC_RELAXED));
  return 0;
}
}
