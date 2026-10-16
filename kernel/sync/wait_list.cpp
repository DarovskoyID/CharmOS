#include "wait.hpp"

namespace kernel::sync {

void WaitList::push(sched::Thread* t) {
  t->wait_next = nullptr;
  if (tail_ != nullptr) {
    tail_->wait_next = t;
  } else {
    head_ = t;
  }
  tail_ = t;
}

sched::Thread* WaitList::pop() {
  sched::Thread* t = head_;
  if (t == nullptr) return nullptr;
  head_ = t->wait_next;
  if (head_ == nullptr) tail_ = nullptr;
  t->wait_next = nullptr;
  return t;
}

bool WaitList::remove(sched::Thread* t) {
  sched::Thread* prev = nullptr;
  for (sched::Thread* cur = head_; cur != nullptr; prev = cur, cur = cur->wait_next) {
    if (cur != t) continue;
    if (prev != nullptr) {
      prev->wait_next = cur->wait_next;
    } else {
      head_ = cur->wait_next;
    }
    if (tail_ == cur) tail_ = prev;
    cur->wait_next = nullptr;
    return true;
  }
  return false;
}

}  // namespace kernel::sync
