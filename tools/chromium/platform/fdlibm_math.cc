// ISO C entrypoints backed by the pinned Chromium fdlibm implementation.
#include "third_party/fdlibm/ieee754.h"
#include <errno.h>
#include <fenv.h>
#include <math.h>

extern "C" {
double acosh(double value) {
  double result=fdlibm::acosh(value);
  if(value<1) { feraiseexcept(FE_INVALID); errno=EDOM; }
  return result;
}
double asinh(double value) { return fdlibm::asinh(value); }
double atanh(double value) {
  double result=fdlibm::atanh(value);
  double magnitude=fabs(value);
  if(magnitude>1) { feraiseexcept(FE_INVALID); errno=EDOM; }
  else if(magnitude==1) { feraiseexcept(FE_DIVBYZERO); errno=ERANGE; }
  return result;
}
double sinh(double value) {
  double result=fdlibm::sinh(value);
  if(isfinite(value) && isinf(result)) { feraiseexcept(FE_OVERFLOW|FE_INEXACT); errno=ERANGE; }
  return result;
}
double cosh(double value) {
  double result=fdlibm::cosh(value);
  if(isfinite(value) && isinf(result)) { feraiseexcept(FE_OVERFLOW|FE_INEXACT); errno=ERANGE; }
  return result;
}
double tanh(double value) { return fdlibm::tanh(value); }
}
