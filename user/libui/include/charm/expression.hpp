#pragma once

namespace charm::math {
enum class Error { None, Syntax, DivisionByZero, Domain, Limit };
struct Result { double value; Error error; unsigned offset; };

// Bounded recursive descent, no allocation or host math library dependency.
class Expression {
  const char* start_; const char* p_;
  Error error_ = Error::None;
  unsigned depth_ = 0, work_ = 0;
  double variable_;
  double parameter_;
  void space() { while (*p_ == ' ' || *p_ == '\t') ++p_; }
  bool take(char c) { space(); if (*p_ != c) return false; ++p_; return true; }
  double fail(Error e) { if (error_ == Error::None) error_ = e; return 0; }
  double bounded(double n) { return n == n && n <= 1e100 && n >= -1e100 ? n : fail(Error::Limit); }
  bool word(const char* s) {
    const char* q = p_;
    while (*s && *q == *s) { ++q; ++s; }
    if (*s || (*q >= 'a' && *q <= 'z')) return false;
    p_ = q; return true;
  }
  double root(double a) {
    if (a < 0) return fail(Error::Domain);
    if (a == 0) return 0;
    double r = a > 1 ? a : 1;
    for (unsigned i = 0; i < 200; ++i) {
      double next = (r + a / r) * 0.5;
      if (next == r) break;
      r = next;
    }
    return r;
  }
  double trig(double a, bool cosine) {
    if (a > 1e6 || a < -1e6) return fail(Error::Limit);
    constexpr double pi = 3.14159265358979323846;
    a -= static_cast<double>(static_cast<long long>(a / (2 * pi))) * (2 * pi);
    if (a > pi) a -= 2 * pi;
    if (a < -pi) a += 2 * pi;
    double term = cosine ? 1 : a, sum = term;
    for (int i = 1; i <= 18; ++i) {
      int k = cosine ? 2 * i - 1 : 2 * i;
      term *= -a * a / (k * (k + 1)); sum += term;
    }
    return sum;
  }
  double primary() {
    space();
    if (take('(')) { double n = sum(); if (!take(')')) return fail(Error::Syntax); return n; }
    if (word("pi")) return 3.14159265358979323846;
    if (word("x")) return variable_;
    if (word("a")) return parameter_;
    if (word("sqrt")) { if (!take('(')) return fail(Error::Syntax); double n = sum(); if (!take(')')) return fail(Error::Syntax); return root(n); }
    if (word("abs")) { if (!take('(')) return fail(Error::Syntax); double n = sum(); if (!take(')')) return fail(Error::Syntax); return n < 0 ? -n : n; }
    bool sine = word("sin");
    bool cosine = !sine && word("cos");
    if (sine || cosine) { if (!take('(')) return fail(Error::Syntax); double n = sum(); if (!take(')')) return fail(Error::Syntax); return trig(n, cosine); }
    double n = 0; bool digit = false;
    while (*p_ >= '0' && *p_ <= '9') { digit = true; n = bounded(n * 10 + *p_++ - '0'); }
    if (take('.')) {
      double scale = 0.1;
      while (*p_ >= '0' && *p_ <= '9') { digit = true; n += (*p_++ - '0') * scale; scale *= 0.1; }
    }
    if (!digit) return fail(Error::Syntax);
    return n;
  }
  double power() {
    double a = primary();
    if (take('^')) {
      double b = unary();
      if (b < -1024 || b > 1024) return fail(Error::Limit);
      long long exponent = static_cast<long long>(b);
      if (b != static_cast<double>(exponent)) return fail(Error::Domain);
      bool inverse = exponent < 0; if (inverse) exponent = -exponent;
      double n = 1;
      while (exponent != 0) { if ((exponent & 1) != 0) n = bounded(n * a); exponent >>= 1; if (exponent) a = bounded(a * a); }
      if (inverse && n == 0) return fail(Error::DivisionByZero);
      return inverse ? 1 / n : n;
    }
    return a;
  }
  double unary() {
    if (++depth_ > 32 || ++work_ > 512) { --depth_; return fail(Error::Limit); }
    double n;
    if (take('+')) n = unary(); else if (take('-')) n = -unary(); else n = power();
    --depth_; return n;
  }
  double product() {
    double n = unary();
    for (;;) {
      if (take('*')) n = bounded(n * unary());
      else if (take('/')) { double b = unary(); if (b == 0) return fail(Error::DivisionByZero); n = bounded(n / b); }
      else return n;
      if (error_ != Error::None) return 0;
    }
  }
  double sum() {
    double n = product();
    for (;;) {
      if (take('+')) n = bounded(n + product());
      else if (take('-')) n = bounded(n - product());
      else return n;
      if (error_ != Error::None) return 0;
    }
  }
 public:
  explicit Expression(const char* text, double x, double a) : start_(text), p_(text), variable_(x), parameter_(a) {}
  Result run() {
    // Bound the input before parsing, including malformed untrusted queries.
    unsigned length = 0;
    while (start_[length] && length < 512) ++length;
    if (length == 512) return {0, Error::Limit, length};
    double value = bounded(sum()); space();
    if (*p_ && error_ == Error::None) error_ = Error::Syntax;
    return {value, error_, static_cast<unsigned>(p_ - start_)};
  }
};
inline Result evaluate(const char* text, double x = 0, double a = 1) { return Expression(text, x, a).run(); }
inline const char* error_text(Error e) {
  switch (e) {
    case Error::None: return "";
    case Error::DivisionByZero: return "Деление на ноль";
    case Error::Domain: return "Недопустимый аргумент";
    case Error::Limit: return "Превышен предел вычисления";
    default: return "Ошибка выражения";
  }
}
}
