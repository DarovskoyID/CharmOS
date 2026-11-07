#include <charm/sys.hpp>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <time.h>

#define CHECK(c) do { if (!(c)) { printf("runtime: FAILED line %d\n", __LINE__); exit(1); } } while (0)
namespace {
void* random_worker(void*) {
  unsigned char a[256], b[256];
  for (unsigned i = 0; i < 8; ++i) {
    if (getentropy(a, sizeof a) || getentropy(b, sizeof b) || !memcmp(a,b,sizeof a))
      return reinterpret_cast<void*>(1);
  }
  return nullptr;
}
}
extern "C" int main(int argc, char** argv) {
  CHECK(argc == 2);
  bool absent = !strcmp(argv[1], "none");
  bool tpm = !strcmp(argv[1], "tpm");
  timespec real, mono, later;
  CHECK(clock_gettime(CLOCK_REALTIME, &real) == 0);
  // QEMU RTC is fixed to 2026-10-17T00:00:00 UTC, then runs normally.
  CHECK(real.tv_sec >= 1792195200 && real.tv_sec < 1792195500);
  CHECK(real.tv_nsec >= 0 && real.tv_nsec < 1000000000);
  CHECK(clock_gettime(CLOCK_MONOTONIC, &mono) == 0);
  CHECK(clock_gettime(99, &later) == -1 && errno == EINVAL);
  CHECK(clock_gettime(CLOCK_REALTIME, nullptr) == -1 && errno == EFAULT);
  timespec delay{0, 20000000};
  CHECK(nanosleep(&delay, nullptr) == 0);
  CHECK(clock_gettime(CLOCK_REALTIME, &later) == 0);
  CHECK(later.tv_sec > real.tv_sec || (later.tv_sec == real.tv_sec && later.tv_nsec > real.tv_nsec));
  CHECK(time(nullptr) >= real.tv_sec);
  CHECK(clock_gettime(CLOCK_MONOTONIC, &later) == 0);
  CHECK(later.tv_sec > mono.tv_sec || (later.tv_sec == mono.tv_sec && later.tv_nsec > mono.tv_nsec));
  printf("runtime: UTC and clock validation ok\n");
  {
    struct sigaction action{},old{};
    memset(&old,0xa5,sizeof old);
    unsigned char saved[sizeof old];
    memcpy(saved,&old,sizeof old);
    CHECK(sigaction(SIGPROF,&action,&old)==-1 && errno==ENOTSUP);
    CHECK(memcmp(&old,saved,sizeof old)==0);
    CHECK(sigaction(0,&action,nullptr)==-1 && errno==EINVAL);
    unsigned char alternate[32];
    stack_t stack{alternate,0,sizeof alternate}, previous{};
    memset(&previous,0xa5,sizeof previous);
    unsigned char untouched[sizeof previous];
    memcpy(untouched,&previous,sizeof previous);
    CHECK(sigaltstack(&stack,&previous)==-1 && errno==ENOTSUP);
    CHECK(memcmp(&previous,untouched,sizeof previous)==0);
    CHECK(sigaltstack(nullptr,&previous)==0 && previous.ss_flags==SS_DISABLE &&
          previous.ss_sp==nullptr && previous.ss_size==0);
    stack.ss_flags=SS_DISABLE;
    CHECK(sigaltstack(&stack,nullptr)==0);
    errno=ERANGE;
    CHECK(pthread_kill(pthread_self(),SIGPROF)==ENOTSUP && errno==ERANGE);
    CHECK(pthread_kill(pthread_self(),0)==ENOTSUP && errno==ERANGE);
    CHECK(pthread_kill(pthread_self(),-1)==EINVAL && errno==ERANGE);
    struct rusage usage;
    memset(&usage,0xa5,sizeof usage);
    unsigned char original[sizeof usage];
    memcpy(original,&usage,sizeof usage);
    CHECK(getrusage(RUSAGE_SELF,&usage)==-1 && errno==ENOTSUP);
    CHECK(memcmp(&usage,original,sizeof usage)==0);
    CHECK(getrusage(RUSAGE_CHILDREN,&usage)==-1 && errno==ENOTSUP);
    CHECK(memcmp(&usage,original,sizeof usage)==0);
    CHECK(getrusage(99,&usage)==-1 && errno==EINVAL);
  }
  // v1.19: W^X executable memory (prerequisite for V8/JIT). mmap RW, write real
  // code, flip to RX, execute it; a simultaneous W+X request must be rejected.
  {
    auto* code = static_cast<unsigned char*>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    CHECK(code != MAP_FAILED);
    static const unsigned char body[] = {0xb8, 42, 0, 0, 0, 0xc3};  // mov eax,42; ret
    memcpy(code, body, sizeof body);
    CHECK(mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0);
    int (*fn)() = reinterpret_cast<int (*)()>(code);
    CHECK(fn() == 42);
    errno = 0;
    CHECK(mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == EINVAL);
    CHECK(munmap(code, 4096) == 0);
    // Protecting one page must not mark the entire allocation read-only and
    // cause a later whole-allocation restore to be skipped by libc.
    auto* pair=static_cast<unsigned char*>(mmap(nullptr,8192,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(pair!=MAP_FAILED);
    CHECK(mprotect(pair,4096,PROT_READ)==0);
    CHECK(mprotect(pair+4096,4096,PROT_READ|PROT_WRITE)==0);
    CHECK(mprotect(pair,8192,PROT_READ|PROT_WRITE)==0);
    pair[0]=17; pair[4096]=29;
    CHECK(pair[0]==17 && pair[4096]==29);
    CHECK(mprotect(pair,SIZE_MAX,PROT_READ)==-1 && errno==EINVAL);
    CHECK(munmap(pair,8192)==0);
    // Inaccessible reservations must fail actual user-copy reads, then
    // restore at the same address. Fixed anonymous replacement must zero
    // every replaced page without touching its neighbour.
    charm::Handle channels[2];
    CHECK(charm::channel_create(channels)==charm::Status::Ok);
    auto* reservation=static_cast<unsigned char*>(mmap(nullptr,8192,PROT_NONE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(reservation!=MAP_FAILED);
    CHECK(charm::channel_write(channels[0],reservation,1,nullptr,0)==charm::Status::BadAddress);
    CHECK(mprotect(reservation,8192,PROT_READ|PROT_WRITE)==0);
    memset(reservation,0xa5,8192);
    CHECK(mprotect(reservation,4096,PROT_NONE)==0);
    CHECK(charm::channel_write(channels[0],reservation,1,nullptr,0)==charm::Status::BadAddress);
    CHECK(reservation[4096]==0xa5);
    CHECK(mprotect(reservation,4096,PROT_READ|PROT_WRITE)==0);
    CHECK(reservation[0]==0xa5);  // Protection alone preserves contents.
    CHECK(mmap(reservation,4096,PROT_NONE,MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==reservation);
    CHECK(charm::channel_write(channels[0],reservation,1,nullptr,0)==charm::Status::BadAddress);
    CHECK(mprotect(reservation,4096,PROT_READ|PROT_WRITE)==0);
    for (size_t i=0;i<4096;++i) CHECK(reservation[i]==0);
    for (size_t i=4096;i<8192;++i) CHECK(reservation[i]==0xa5);
    CHECK(mprotect(reservation,8192,PROT_NONE)==0);
    CHECK(munmap(reservation,8192)==0);  // Unmap non-present leaves too.
    CHECK(charm::handle_close(channels[0])==charm::Status::Ok);
    CHECK(charm::handle_close(channels[1])==charm::Status::Ok);
    printf("runtime: inaccessible and zeroed replacement memory ok\n");
    // ABI v1.21: VmoReserve is lazy. An inaccessible alias installs zero
    // markers; committing through one alias and restoring access through the
    // other must refresh the physical frame rather than expose address zero.
    auto lazy=charm::vmo_reserve(8192);
    CHECK(lazy.ok());
    auto lazy_handle=static_cast<charm::Handle>(lazy.value);
    auto alias_a=charm::vmo_map(lazy_handle,0);
    auto alias_b=charm::vmo_map(lazy_handle,0);
    CHECK(alias_a.ok() && alias_b.ok());
    CHECK(charm::vmo_protect(alias_a.value,4096,charm::rights::kRead|charm::rights::kWrite)==charm::Status::Ok);
    CHECK(charm::vmo_protect(alias_b.value,4096,charm::rights::kRead)==charm::Status::Ok);
    auto* page_a=reinterpret_cast<unsigned char*>(alias_a.value);
    auto* page_b=reinterpret_cast<unsigned char*>(alias_b.value);
    page_a[0]=0x5a; page_a[4095]=0xa5;
    CHECK(page_b[0]==0x5a && page_b[4095]==0xa5);
    CHECK(charm::vmo_unmap(alias_a.value)==charm::Status::Ok);
    CHECK(charm::vmo_unmap(alias_b.value)==charm::Status::Ok);
    CHECK(charm::handle_close(lazy_handle)==charm::Status::Ok);
    // A large PROT_NONE reservation followed by a small fixed replacement must
    // not require committing the untouched rest.
    size_t budget=32u<<20;
    auto* sparse=static_cast<unsigned char*>(mmap(nullptr,budget,PROT_NONE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(sparse!=MAP_FAILED);
    CHECK(mmap(sparse+budget-8192,8192,PROT_NONE,
        MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==sparse+budget-8192);
    CHECK(mprotect(sparse+budget-4096,4096,PROT_READ|PROT_WRITE)==0);
    for (size_t i=0;i<4096;++i) CHECK(sparse[budget-4096+i]==0);
    CHECK(mprotect(sparse+budget-4096,4096,PROT_NONE)==0);
    CHECK(munmap(sparse,budget)==0);
    printf("runtime: lazy VMO reservation and alias commit ok\n");
    // ABI v1.22: partial munmap removes only the selected leaves; the rest of
    // the mapping stays usable, and mprotect over a hole fails as ENOMEM.
    auto* partial=static_cast<unsigned char*>(mmap(nullptr,3*4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(partial!=MAP_FAILED);
    partial[0]=1; partial[4096]=2; partial[8192]=3;
    CHECK(munmap(partial+4096,4096)==0);
    errno=0;
    CHECK(mprotect(partial+4096,4096,PROT_READ|PROT_WRITE)==-1 && errno==ENOMEM);
    CHECK(mprotect(partial,4096,PROT_READ|PROT_WRITE)==0);
    CHECK(mprotect(partial+8192,4096,PROT_READ|PROT_WRITE)==0);
    CHECK(partial[0]==1 && partial[8192]==3);
    CHECK(munmap(partial+4096,4096)==0);  // second unmap of the hole is a no-op
    CHECK(munmap(partial,3*4096)==0);     // full mapping tears everything down
    printf("runtime: partial munmap contract ok\n");
    // A read-only transferred/duplicated capability cannot gain Write or
    // Exec through VmoProtect, even after its handle has been closed.
    auto object=charm::vmo_create(4096);
    CHECK(object.ok());
    auto original=static_cast<charm::Handle>(object.value);
    auto executable=charm::handle_duplicate(original,charm::rights::kRead|charm::rights::kMap|charm::rights::kExec);
    CHECK(executable.ok());
    auto exec_handle=static_cast<charm::Handle>(executable.value);
    auto exec_mapping=charm::vmo_map(exec_handle,charm::rights::kRead|charm::rights::kExec);
    CHECK(exec_mapping.ok());
    CHECK(charm::vmo_unmap(exec_mapping.value)==charm::Status::Ok);
    CHECK(charm::handle_close(exec_handle)==charm::Status::Ok);
    auto readonly=charm::handle_duplicate(original,charm::rights::kRead|charm::rights::kMap);
    CHECK(readonly.ok());
    auto restricted=static_cast<charm::Handle>(readonly.value);
    auto mapping=charm::vmo_map(restricted,charm::rights::kRead);
    CHECK(mapping.ok());
    CHECK(charm::handle_close(restricted)==charm::Status::Ok);
    CHECK(charm::vmo_protect(mapping.value,4096,0)==charm::Status::Ok);
    CHECK(charm::vmo_protect(mapping.value,4096,charm::rights::kRead|charm::rights::kWrite)
          ==charm::Status::AccessDenied);
    CHECK(charm::vmo_protect(mapping.value,4096,charm::rights::kRead|charm::rights::kExec)
          ==charm::Status::AccessDenied);
    CHECK(charm::vmo_protect(mapping.value,4096,charm::rights::kRead)==charm::Status::Ok);
    CHECK(charm::vmo_protect(mapping.value,UINT64_MAX-4095,charm::rights::kRead)
          ==charm::Status::InvalidArgs);
    CHECK(charm::vmo_unmap(mapping.value)==charm::Status::Ok);
    CHECK(charm::handle_close(original)==charm::Status::Ok);
    printf("runtime: W^X executable memory ok\n");
  }
  unsigned char buffer[600];
  memset(buffer, 0xa5, sizeof buffer);
  CHECK(charm::random_read(buffer, 257).status == charm::Status::InvalidArgs);
  CHECK(charm::random_read(buffer, 1, 2).status == charm::Status::InvalidArgs);
  CHECK(charm::random_read(nullptr, 0).ok());
  CHECK(getrandom(buffer, 0, 2) == -1 && errno == EINVAL);
  CHECK(getentropy(buffer, 257) == -1 && errno == EIO);
  CHECK(buffer[0] == 0xa5);
  if (absent) {
    CHECK(charm::random_read(buffer, 32).status == charm::Status::NotSupported);
    CHECK(getentropy(buffer, 32) == -1 && errno == ENOSYS);
    CHECK(buffer[0] == 0xa5);
    printf("runtime: missing entropy fails safely\n");
    return 0;
  }
  if (tpm) CHECK(charm::random_read(buffer, 32, 1).status == charm::Status::WouldBlock);
  else CHECK(getrandom(buffer, 32, GRND_NONBLOCK) == 32);
  CHECK(charm::random_read(reinterpret_cast<void*>(0xffff800000000000ull), 32).status == charm::Status::BadAddress);
  CHECK(getrandom(buffer, sizeof buffer, 0) == sizeof buffer);
  CHECK(memcmp(buffer, buffer + 256, 256) != 0);
  pthread_t threads[4];
  for (auto& thread : threads) CHECK(pthread_create(&thread, nullptr, random_worker, nullptr) == 0);
  for (auto thread : threads) {
    void* result;
    CHECK(pthread_join(thread, &result) == 0 && result == nullptr);
  }
  printf("runtime: entropy bounds and parallel clients ok\n");
  return 0;
}
