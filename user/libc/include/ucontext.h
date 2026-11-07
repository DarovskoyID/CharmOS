#ifndef _UCONTEXT_H
#define _UCONTEXT_H
#include <signal.h>
/* CharmOS сохраняет при сигнале только регистры текущего кадра, а не полный
   машинный контекст. Раскладка совместима с x86-64 POSIX настолько, чтобы
   V8 sampler компилировался; SIGPROF на CharmOS не генерируется. */
typedef struct ucontext_t {
  unsigned long uc_flags;
  struct ucontext_t* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  sigset_t uc_sigmask;
} ucontext_t;
#endif
