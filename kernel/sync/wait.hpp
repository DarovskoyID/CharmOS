#pragma once
// Блокирующие примитивы: поток, который не может продолжить, уходит с CPU
// (в отличие от spinlock). Нельзя использовать в обработчиках прерываний и
// держа spinlock — schedule() это проверяет и паникует.

#include <stdint.h>

#include "../sched/sched.hpp"
#include "spinlock.hpp"

namespace kernel::sync {

// Очередь ожидающих потоков (FIFO).
class WaitList {
 public:
  void push(sched::Thread* t);
  sched::Thread* pop();
  bool empty() const { return head_ == nullptr; }

 private:
  sched::Thread* head_ = nullptr;
  sched::Thread* tail_ = nullptr;
};

class Mutex {
 public:
  explicit constexpr Mutex(const char* name) : lock_(LockRank::Wait, name) {}
  void lock();
  void unlock();
  bool held_by_current() const;

 private:
  Spinlock lock_;
  sched::Thread* owner_ = nullptr;
  WaitList waiters_;
};

class MutexGuard {
 public:
  explicit MutexGuard(Mutex& m) : m_(m) { m_.lock(); }
  ~MutexGuard() { m_.unlock(); }
  MutexGuard(const MutexGuard&) = delete;
  MutexGuard& operator=(const MutexGuard&) = delete;

 private:
  Mutex& m_;
};

class Semaphore {
 public:
  constexpr Semaphore(const char* name, uint64_t initial)
      : lock_(LockRank::Wait, name), count_(initial) {}
  void down();
  void up();

 private:
  Spinlock lock_;
  uint64_t count_;
  WaitList waiters_;
};

}  // namespace kernel::sync
