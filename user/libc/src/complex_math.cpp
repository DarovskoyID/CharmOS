// Double-precision error function and compiler-rt complex arithmetic helpers.
//
// Chromium's C++ runtime can emit calls to __divdc3/__muldc3/__mulsc3 for
// complex arithmetic, and Chromium's graphics/metrics code uses erf/erfcf.
// CharmOS provides real implementations here instead of pulling compiler-rt:
// erf uses a Taylor series on the convergent interval and a Lentz continued
// fraction for the complementary function; the complex helpers follow the
// scaled algorithms from compiler-rt so overflow recovery is well defined.
#include <errno.h>
#include <float.h>
#include <math.h>

namespace {

constexpr double kInvSqrtPi = 0.5641895835477562869480794515607725858;

// erf(x) = 2/sqrt(pi) * sum_{n>=0} (-1)^n x^(2n+1) / (n! (2n+1)).
double ErfSeries(double x) {
  const double x2 = x * x;
  double term = x;
  double sum = x;
  for (int n = 1; n < 80; ++n) {
    term *= -x2 / n;
    const double contribution = term / (2 * n + 1);
    sum += contribution;
    if (fabs(contribution) <= fabs(sum) * DBL_EPSILON) break;
  }
  return 2.0 * kInvSqrtPi * sum;
}

// erfcx(x) = exp(x^2) * erfc(x) via the continued fraction
//   erfcx(x) = 1/sqrt(pi) * 1/(x + 1/2/(x + 1/(x + 3/2/(x + ...)))
// evaluated with the modified Lentz algorithm (DLMF 7.9.3).
double ErfcxContinuedFraction(double x) {
  constexpr double kTiny = 1e-300;
  double f = x;
  double c = x;
  double d = 0.0;
  for (int n = 1; n < 400; ++n) {
    const double a = n * 0.5;
    const double b = x;
    d = b + a * d;
    if (d == 0.0) d = kTiny;
    d = 1.0 / d;
    c = b + a / c;
    if (c == 0.0) c = kTiny;
    const double delta = c * d;
    f *= delta;
    if (fabs(delta - 1.0) <= 1e-16) break;
  }
  return f;
}

double ErfcContinuedFraction(double x) {
  return exp(-x * x) * kInvSqrtPi / ErfcxContinuedFraction(x);
}

double ErfcImpl(double x) {
  if (isnan(x)) return x;
  if (x < 0.0) return 2.0 - ErfcImpl(-x);
  if (x < 0.5) return 1.0 - ErfSeries(x);
  if (x > 27.0) return 0.0;
  return ErfcContinuedFraction(x);
}

}  // namespace

extern "C" double erf(double x) {
  const double magnitude = fabs(x);
  if (isnan(x)) return x + x;
  if (magnitude >= 6.0) return x > 0 ? 1.0 : -1.0;
  const double value = (magnitude < 1.5)
                           ? ErfSeries(magnitude)
                           : 1.0 - ErfcImpl(magnitude);
  return x < 0 ? -value : value;
}

extern "C" double erfc(double x) { return ErfcImpl(x); }

extern "C" float erfcf(float x) {
  if (isnan(x)) return x + x;
  const double value = ErfcImpl(static_cast<double>(x));
  if (value < 0.0) {
    errno = ERANGE;
    return 0.0f;
  }
  return static_cast<float>(value);
}

// --- compiler-rt complex helpers ------------------------------------------

extern "C" double _Complex __muldc3(double a, double b, double c, double d) {
  const double ac = a * c;
  const double bd = b * d;
  const double ad = a * d;
  const double bc = b * c;
  double real = ac - bd;
  double imag = ad + bc;
  if (isnan(ac) || isnan(bd) || isnan(ad) || isnan(bc)) {
    bool recalc = false;
    if (isinf(a) || isinf(b)) {
      a = copysign(isinf(a) ? 1.0 : 0.0, a);
      b = copysign(isinf(b) ? 1.0 : 0.0, b);
      if (isnan(c)) c = copysign(0.0, c);
      if (isnan(d)) d = copysign(0.0, d);
      recalc = true;
    }
    if (isinf(c) || isinf(d)) {
      c = copysign(isinf(c) ? 1.0 : 0.0, c);
      d = copysign(isinf(d) ? 1.0 : 0.0, d);
      if (isnan(a)) a = copysign(0.0, a);
      if (isnan(b)) b = copysign(0.0, b);
      recalc = true;
    }
    if (!recalc &&
        (isinf(ac) || isinf(bd) || isinf(ad) || isinf(bc))) {
      if (isnan(a)) a = copysign(0.0, a);
      if (isnan(b)) b = copysign(0.0, b);
      if (isnan(c)) c = copysign(0.0, c);
      if (isnan(d)) d = copysign(0.0, d);
      recalc = true;
    }
    if (recalc) {
      real = INFINITY * (a * c - b * d);
      imag = INFINITY * (a * d + b * c);
    }
  }
  double _Complex result;
  __real__ result = real;
  __imag__ result = imag;
  return result;
}

extern "C" double _Complex __divdc3(double a, double b, double c, double d) {
  int ilogbw = 0;
  const double logbw = __builtin_logb(c > 0 ? c : -c);
  if (isfinite(logbw)) {
    ilogbw = static_cast<int>(logbw);
    c = __builtin_scalbn(c, -ilogbw);
    d = __builtin_scalbn(d, -ilogbw);
  }
  const double denom = c * c + d * d;
  double real = __builtin_scalbn((a * c + b * d) / denom, -ilogbw);
  double imag = __builtin_scalbn((b * c - a * d) / denom, -ilogbw);
  if (isnan(real) && isnan(imag)) {
    if (denom == 0.0) {
      real = copysign(INFINITY, c) * a;
      imag = copysign(INFINITY, c) * b;
    } else if (isinf(denom)) {
      a = copysign(isinf(a) ? 1.0 : 0.0, a);
      b = copysign(isinf(b) ? 1.0 : 0.0, b);
      c = copysign(isinf(c) ? 1.0 : 0.0, c);
      d = copysign(isinf(d) ? 1.0 : 0.0, d);
      real = INFINITY * (a * c + b * d);
      imag = INFINITY * (b * c - a * d);
    }
  }
  double _Complex result;
  __real__ result = real;
  __imag__ result = imag;
  return result;
}

extern "C" float _Complex __mulsc3(float a, float b, float c, float d) {
  const double _Complex product = __muldc3(a, b, c, d);
  float _Complex result;
  __real__ result = static_cast<float>(__real__ product);
  __imag__ result = static_cast<float>(__imag__ product);
  return result;
}
