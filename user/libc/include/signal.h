#ifndef _SIGNAL_H
#define _SIGNAL_H
/* Сигналы CharmOS (docs/abi/process.md §9): номера — как в ядре. */
#define SIGINT 1
#define SIGQUIT 2
#define SIGTERM 3
#define SIGCHLD 4
#define SIGSEGV 5
#define SIGALRM 6
#define SIGUSR1 7
#define SIGUSR2 8
#define NSIG 9
typedef void (*sighandler_t)(int);
typedef int sig_atomic_t;
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
typedef unsigned long sigset_t;
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#ifdef __cplusplus
extern "C" {
#endif
sighandler_t signal(int signo, sighandler_t handler);
int sigprocmask(int how, const sigset_t* set, sigset_t* old);
int sigemptyset(sigset_t* s);
int sigfillset(sigset_t* s);
int sigaddset(sigset_t* s, int signo);
unsigned alarm(unsigned seconds);
#ifdef __cplusplus
}
#endif
#endif
