#include "../../kernel/sync/wait.hpp"
#include "test.hpp"

using kernel::sched::Thread;
using kernel::sync::WaitList;

TEST(wait_list_interrupted_ready_thread_keeps_ready_links) {
  Thread a{}, b{}, c{}, ready{};
  WaitList list;
  list.push(&a); list.push(&b); list.push(&c);
  // interrupt() can enqueue b while it is still in this wait list. A later
  // wake/pop/remove must not touch b's ready-queue link (the original bug).
  b.next = &ready;
  CHECK(list.remove(&b));
  CHECK(b.next == &ready);
  CHECK(list.pop() == &a && list.pop() == &c && list.pop() == nullptr);
  list.push(&b);
  CHECK(list.pop() == &b && b.next == &ready);
}

TEST(wait_list_fifo_and_remove_boundaries) {
  Thread a{}, b{}, c{}; WaitList list;
  CHECK(list.empty() && !list.remove(&a));
  list.push(&a); list.push(&b); list.push(&c);
  CHECK(list.remove(&a) && list.remove(&c));
  CHECK(list.pop() == &b && list.empty());
  list.push(&c); CHECK(list.pop() == &c && list.empty());
}
