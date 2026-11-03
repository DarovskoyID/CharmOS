#pragma once
#if !defined(__x86_64__)
#error "Native nonlocal jumps require an architecture implementation"
#endif
#include <arch/x86_64/setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif

// A macro as required by the C interface; the external function also exists.
#define setjmp(env) setjmp(env)
