// The upstream hardware sqrt specializations return IEEE results/flags but
// do not set errno. These C entrypoints retain the native C error contract.
#include <errno.h>
#include <math.h>
#include "src/math/sqrt.h"
#include "src/math/sqrtf.h"
#include "src/math/sqrtl.h"
extern "C" double sqrt(double x) {
  double result=LIBC_NAMESPACE::sqrt(x);
  if(x<0) errno=EDOM;
  return result;
}
extern "C" float sqrtf(float x) {
  float result=LIBC_NAMESPACE::sqrtf(x);
  if(x<0) errno=EDOM;
  return result;
}
extern "C" long double sqrtl(long double x) {
  long double result=LIBC_NAMESPACE::sqrtl(x);
  if(x<0) errno=EDOM;
  return result;
}
