#ifndef _CHARMOS_MATH_H
#define _CHARMOS_MATH_H
typedef float float_t;
typedef double double_t;
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x,y) __builtin_isgreater((x),(y))
#define isgreaterequal(x,y) __builtin_isgreaterequal((x),(y))
#define isless(x,y) __builtin_isless((x),(y))
#define islessequal(x,y) __builtin_islessequal((x),(y))
#define islessgreater(x,y) __builtin_islessgreater((x),(y))
#define isunordered(x,y) __builtin_isunordered((x),(y))
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#if defined(__NO_MATH_ERRNO__) && !defined(__CHARMOS_NUMERIC_RUNTIME__)
#define math_errhandling MATH_ERREXCEPT
#else
#define math_errhandling (MATH_ERRNO | MATH_ERREXCEPT)
#endif
#define M_E 2.71828182845904523536
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_LOG2E 1.44269504088896340736
#define M_LOG10E 0.43429448190325182765
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440
#ifdef __cplusplus
extern "C" {
#endif
#define __CHARM_MATH_UNARY(name) double name(double); float name##f(float); long double name##l(long double);
#define __CHARM_MATH_BINARY(name) double name(double,double); float name##f(float,float); long double name##l(long double,long double);
__CHARM_MATH_UNARY(acos)
__CHARM_MATH_UNARY(asin)
__CHARM_MATH_UNARY(atan)
__CHARM_MATH_UNARY(acosh)
__CHARM_MATH_UNARY(asinh)
__CHARM_MATH_UNARY(atanh)
__CHARM_MATH_UNARY(cos)
__CHARM_MATH_UNARY(sin)
__CHARM_MATH_UNARY(tan)
__CHARM_MATH_UNARY(cosh)
__CHARM_MATH_UNARY(sinh)
__CHARM_MATH_UNARY(tanh)
__CHARM_MATH_UNARY(exp)
__CHARM_MATH_UNARY(exp2)
__CHARM_MATH_UNARY(expm1)
__CHARM_MATH_UNARY(log)
__CHARM_MATH_UNARY(log10)
__CHARM_MATH_UNARY(log2)
__CHARM_MATH_UNARY(log1p)
__CHARM_MATH_UNARY(logb)
__CHARM_MATH_UNARY(sqrt)
__CHARM_MATH_UNARY(cbrt)
__CHARM_MATH_UNARY(fabs)
__CHARM_MATH_UNARY(ceil)
__CHARM_MATH_UNARY(floor)
__CHARM_MATH_UNARY(trunc)
__CHARM_MATH_UNARY(round)
__CHARM_MATH_UNARY(rint)
__CHARM_MATH_UNARY(nearbyint)
__CHARM_MATH_UNARY(erf)
__CHARM_MATH_UNARY(erfc)
__CHARM_MATH_UNARY(tgamma)
__CHARM_MATH_UNARY(lgamma)
__CHARM_MATH_BINARY(atan2)
__CHARM_MATH_BINARY(pow)
__CHARM_MATH_BINARY(hypot)
__CHARM_MATH_BINARY(fmod)
__CHARM_MATH_BINARY(remainder)
__CHARM_MATH_BINARY(copysign)
__CHARM_MATH_BINARY(nextafter)
__CHARM_MATH_BINARY(fmax)
__CHARM_MATH_BINARY(fmin)
__CHARM_MATH_BINARY(fdim)
#undef __CHARM_MATH_UNARY
#undef __CHARM_MATH_BINARY
#define __CHARM_MATH_TYPED(type, suffix) \
 type frexp##suffix(type,int*); type ldexp##suffix(type,int); \
 type scalbn##suffix(type,int); type scalbln##suffix(type,long); \
 type modf##suffix(type,type*); type remquo##suffix(type,type,int*); \
 type fma##suffix(type,type,type); int ilogb##suffix(type); \
 long lrint##suffix(type); long long llrint##suffix(type); \
 long lround##suffix(type); long long llround##suffix(type); \
 type nexttoward##suffix(type,long double);
__CHARM_MATH_TYPED(double, )
__CHARM_MATH_TYPED(float, f)
__CHARM_MATH_TYPED(long double, l)
#undef __CHARM_MATH_TYPED
double nan(const char*); float nanf(const char*); long double nanl(const char*);
#define FP_ILOGB0 (-2147483647-1)
#define FP_ILOGBNAN (-2147483647-1)
#ifdef __cplusplus
}
#endif
#endif
