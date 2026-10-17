#ifndef _CHARM_ASSERT_DECLARATIONS
#define _CHARM_ASSERT_DECLARATIONS
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((noreturn)) void __charm_assert_fail(const char* expression, const char* file, int line, const char* function);
#ifdef __cplusplus
}
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && __STDC_VERSION__ < 202311L
#define static_assert _Static_assert
#endif
#endif

#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : __charm_assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif
