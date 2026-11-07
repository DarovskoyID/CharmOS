#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <stddef.h>
#include <sys/types.h>
/* Сигналы CharmOS (docs/abi/process.md §9): номера — как в ядре. */
#define SIGINT 1
#define SIGQUIT 2
#define SIGTERM 3
#define SIGCHLD 4
#define SIGSEGV 5
#define SIGALRM 6
#define SIGUSR1 7
#define SIGUSR2 8
#define SIGKILL 9
#define SIGSTOP 19
#define NSIG 32
/* Source compatibility only: SIGPROF is not delivered. sigaction reports
   ENOTSUP; callers must disable sampling when registration fails. */
#define SIGPROF 27
typedef void (*sighandler_t)(int);
typedef int sig_atomic_t;
typedef struct { int si_signo; int si_errno; int si_code; } siginfo_t;
#define SA_SIGINFO 4
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
typedef unsigned long sigset_t;
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
/* Alternate stacks are not implemented; installation returns ENOTSUP.
   Querying the disabled state and disabling an absent stack are supported. */
typedef struct {
  void* ss_sp;
  int ss_flags;
  size_t ss_size;
} stack_t;
#define SIGSTKSZ 16384
#define MINSIGSTKSZ 2048
#define SS_ONSTACK 1
#define SS_DISABLE 2
/* Минимальное машинное состояние для V8 sampler (x86-64 Linux-совместимая
   раскладка gregs[]). CharmOS не генерирует SIGPROF, поэтому значения не
   читаются; тип нужен для компиляции обработчика. */
typedef struct { long gregs[23]; } mcontext_t;
#define REG_RBP 10
#define REG_RSP 15
#define REG_RIP 16
struct sigaction {
  union {
    sighandler_t sa_handler;
    void (*sa_sigaction)(int, siginfo_t*, void*);
  };
  sigset_t sa_mask;
  int sa_flags;
  void (*sa_restorer)(void);
};
#ifdef __cplusplus
extern "C" {
#endif
sighandler_t signal(int signo, sighandler_t handler);
int sigprocmask(int how, const sigset_t* set, sigset_t* old);
int sigemptyset(sigset_t* s);
int sigfillset(sigset_t* s);
int sigaddset(sigset_t* s, int signo);
int sigaltstack(const stack_t* ss, stack_t* old_ss);
int sigaction(int signo, const struct sigaction* act, struct sigaction* old);
int kill(pid_t pid, int signo);
int raise(int signo);
unsigned alarm(unsigned seconds);
#ifdef __cplusplus
}
#endif
#endif
