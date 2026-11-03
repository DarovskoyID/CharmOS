#include <setjmp.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <fenv.h>

int TestNativeNonlocalJumps(void);

__attribute__((noinline,noreturn,disable_tail_calls))
static void jump_deep(jmp_buf target, int value, unsigned depth) {
  volatile unsigned marker=depth;
  if(depth) jump_deep(target,value,depth-1);
  // Keep this frame live; jumping bypasses it without unwinding.
  if(marker!=depth) __builtin_trap();
  longjmp(target,value);
}

static int check_values(void) {
#define VALUE_PROBE(value,expected) do { \
    jmp_buf target; \
    volatile int jumped=0; \
    if(setjmp(target)==expected) { \
      if(!jumped) return 1; \
    } else { \
      if(jumped) return 1; \
      jumped=1; \
      jump_deep(target,value,16); \
    } \
  } while(0)
  VALUE_PROBE(0,1);
  VALUE_PROBE(1,1);
  VALUE_PROBE(-7,-7);
  VALUE_PROBE(INT_MIN,INT_MIN);
  VALUE_PROBE(INT_MAX,INT_MAX);
#undef VALUE_PROBE
  return 0;
}

static int check_nested(void) {
  jmp_buf outer,inner;
  volatile unsigned stage=0;
  if(setjmp(outer)==0) {
    if(setjmp(inner)==0) {
      stage=1;
      jump_deep(inner,3,7);
    }
    if(stage!=1) return 2;
    stage=2;
    jump_deep(outer,9,9);
  }
  if(stage!=2) return 3;
  for(unsigned i=0;i<1000;++i) {
    if(setjmp(outer)==0) jump_deep(outer,1,i%8);
  }
  return 0;
}

static void* thread_probe(void* argument) {
  uintptr_t index=(uintptr_t)argument;
  jmp_buf target;
  volatile unsigned count=0;
  int mode=(index&1) ? FE_DOWNWARD : FE_UPWARD;
  if(fesetround(FE_TONEAREST)) return (void*)(uintptr_t)4;
  if(setjmp(target)==0) {
    count=1;
    if(fesetround(mode) || feraiseexcept(FE_INVALID)) return (void*)(uintptr_t)5;
    jump_deep(target,1,8);
  }
  if(count!=1 || fegetround()!=mode || !(fetestexcept(FE_INVALID)&FE_INVALID))
    return (void*)(uintptr_t)6;
  if(check_values() || check_nested()) return (void*)(uintptr_t)7;
  return 0;
}

int TestNativeNonlocalJumps(void) {
  if(check_values() || check_nested()) return 8;
  pthread_t workers[8];
  unsigned started=0;
  for(;started<8;++started)
    if(pthread_create(&workers[started],0,thread_probe,(void*)(uintptr_t)started)) break;
  int failure=started!=8 ? 9 : 0;
  for(unsigned i=0;i<started;++i) {
    void* result=0;
    if(pthread_join(workers[i],&result) || result) failure=10;
  }
  return failure;
}
