#include "wait.hpp"

#include "../core/panic.hpp"

namespace kernel::sync {

void WaitList::push(sched::Thread* t) {
  t->next = nullptr;
  if (tail_ != nullptr) {
    tail_->next = t;
  } else {
    head_ = t;
  }
  tail_ = t;
}

sched::Thread* WaitList::pop() {
  sched::Thread* t = head_;
  if (t == nullptr) return nullptr;
  head_ = t->next;
  if (head_ == nullptr) tail_ = nullptr;
  t->next = nullptr;
  return t;
}

bool WaitList::remove(sched::Thread* t) {
  sched::Thread* prev = nullptr;
  for (sched::Thread* cur = head_; cur != nullptr; prev = cur, cur = cur->next) {
    if (cur != t) continue;
    if (prev != nullptr) {
      prev->next = cur->next;
    } else {
      head_ = cur->next;
    }
    if (tail_ == cur) tail_ = prev;
    cur->next = nullptr;
    return true;
  }
  return false;
}

namespace {

// Общая часть ожидания: вызывающий держит lock (прерывания запрещены),
// уже поставил себя в очередь. Блокируемся, отпускаем lock, не включая
// прерывания, уходим с CPU; после пробуждения восстанавливаем флаг.
void sleep_on(Spinlock& lock, bool irq) {
  sched::block_current();
  lock.unlock(false);
  sched::schedule();
  if (irq) asm volatile("sti" ::: "memory");
}

}  // namespace

void Mutex::lock() {
  bool irq = lock_.lock();
  sched::Thread* me = sched::current();
  if (owner_ == me) panic("mutex {}: recursive lock", lock_.name());
  if (owner_ == nullptr) {
    owner_ = me;
    lock_.unlock(irq);
    return;
  }
  waiters_.push(me);
  sleep_on(lock_, irq);
  // unlock() передал владение напрямую нам.
}

void Mutex::unlock() {
  bool irq = lock_.lock();
  if (owner_ != sched::current()) panic("mutex {}: unlock by non-owner", lock_.name());
  sched::Thread* next = waiters_.pop();
  owner_ = next;
  if (next != nullptr) sched::wake(next);
  lock_.unlock(irq);
}

bool Mutex::held_by_current() const { return owner_ == sched::current(); }

void Semaphore::down() {
  bool irq = lock_.lock();
  if (count_ > 0) {
    --count_;
    lock_.unlock(irq);
    return;
  }
  waiters_.push(sched::current());
  sleep_on(lock_, irq);
  // up() передал единицу напрямую нам.
}

void Semaphore::up() {
  bool irq = lock_.lock();
  sched::Thread* next = waiters_.pop();
  if (next != nullptr) {
    sched::wake(next);
  } else {
    ++count_;
  }
  lock_.unlock(irq);
}

}  // namespace kernel::sync
