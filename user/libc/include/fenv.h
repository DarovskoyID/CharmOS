#ifndef _CHARMOS_FENV_H
#define _CHARMOS_FENV_H
typedef struct { unsigned char opaque[32]; } fenv_t;
typedef unsigned short fexcept_t;
#define FE_DIVBYZERO 1
#define FE_INEXACT 2
#define FE_INVALID 4
#define FE_OVERFLOW 8
#define FE_UNDERFLOW 16
#define FE_DENORM 32
#define FE_ALL_EXCEPT 63
#define FE_TONEAREST 0
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00
#define FE_DFL_ENV ((fenv_t*)-1)
#ifdef __cplusplus
extern "C" {
#endif
int feclearexcept(int);
int fegetexceptflag(fexcept_t*, int);
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t*, int);
int fetestexcept(int);
int fegetround(void);
int fesetround(int);
int fegetenv(fenv_t*);
int feholdexcept(fenv_t*);
int fesetenv(const fenv_t*);
int feupdateenv(const fenv_t*);
#ifdef __cplusplus
}
#endif
#endif
