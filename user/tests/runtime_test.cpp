#include <charm/sys.hpp>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
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
