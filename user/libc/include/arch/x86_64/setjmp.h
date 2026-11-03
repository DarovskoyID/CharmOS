#pragma once
#include <stdint.h>

typedef struct {
  uint64_t __rbx, __rbp, __r12, __r13, __r14, __r15, __rsp, __rip;
} __charm_jump_state;
typedef __charm_jump_state jmp_buf[1];

#ifdef __cplusplus
#include <stddef.h>
static_assert(sizeof(__charm_jump_state)==64);
static_assert(alignof(__charm_jump_state)==8);
static_assert(offsetof(__charm_jump_state,__rbx)==0);
static_assert(offsetof(__charm_jump_state,__rbp)==8);
static_assert(offsetof(__charm_jump_state,__r12)==16);
static_assert(offsetof(__charm_jump_state,__r13)==24);
static_assert(offsetof(__charm_jump_state,__r14)==32);
static_assert(offsetof(__charm_jump_state,__r15)==40);
static_assert(offsetof(__charm_jump_state,__rsp)==48);
static_assert(offsetof(__charm_jump_state,__rip)==56);
#endif
