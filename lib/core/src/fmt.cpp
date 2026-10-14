#include <lib/core/fmt.hpp>

namespace lib::core {

void BufferSink::write(const char* data, size_t size) {
  if (cap_ == 0) {
    truncated_ = truncated_ || size > 0;
    return;
  }
  size_t room = cap_ - 1 - len_;
  size_t n = size < room ? size : room;
  for (size_t i = 0; i < n; ++i) buf_[len_ + i] = data[i];
  len_ += n;
  buf_[len_] = '\0';
  if (n < size) truncated_ = true;
}

namespace {

constexpr char kDigits[] = "0123456789abcdef";

void write_unsigned(Sink& sink, uint64_t v, unsigned base, size_t min_digits) {
  char tmp[64];
  size_t n = 0;
  do {
    tmp[n++] = kDigits[v % base];
    v /= base;
  } while (v != 0);
  while (n < min_digits) tmp[n++] = '0';
  char out[64];
  for (size_t i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
  sink.write(out, n);
}

void write_hex(Sink& sink, uint64_t v, bool full) {
  sink.write("0x", 2);
  write_unsigned(sink, v, 16, full ? 16 : 1);
}

void write_arg(Sink& sink, const FmtArg& a, char spec) {
  using Kind = FmtArg::Kind;
  bool hex = spec == 'x' || spec == 'X';
  bool full = spec == 'X';
  switch (a.kind()) {
    case Kind::None:
      sink.write("<?>", 3);
      return;
    case Kind::Unsigned:
      if (hex) {
        write_hex(sink, a.as_unsigned(), full);
      } else {
        write_unsigned(sink, a.as_unsigned(), 10, 1);
      }
      return;
    case Kind::Signed: {
      int64_t s = a.as_signed();
      if (hex) {
        write_hex(sink, static_cast<uint64_t>(s), full);
        return;
      }
      // Модуль через беззнаковое: корректно и для INT64_MIN.
      uint64_t mag = s < 0 ? 0 - static_cast<uint64_t>(s) : static_cast<uint64_t>(s);
      if (s < 0) sink.write("-", 1);
      write_unsigned(sink, mag, 10, 1);
      return;
    }
    case Kind::Bool:
      if (a.as_unsigned() != 0) {
        sink.write("true", 4);
      } else {
        sink.write("false", 5);
      }
      return;
    case Kind::Char: {
      char c = static_cast<char>(a.as_unsigned());
      sink.write(&c, 1);
      return;
    }
    case Kind::String: {
      StringView s = a.as_string();
      sink.write(s.data(), s.size());
      return;
    }
    case Kind::Pointer:
      write_hex(sink, a.as_unsigned(), spec != 'x');
      return;
  }
}

}  // namespace

void vformat(Sink& sink, StringView fmt, const FmtArg* args, size_t count) {
  size_t next = 0;
  size_t run = 0;  // начало ещё не выведенного литерального участка
  size_t i = 0;
  while (i < fmt.size()) {
    char c = fmt[i];
    if ((c == '{' || c == '}') && i + 1 < fmt.size() && fmt[i + 1] == c) {
      sink.write(fmt.data() + run, i - run + 1);
      i += 2;
      run = i;
      continue;
    }
    if (c != '{') {
      ++i;
      continue;
    }
    size_t close = i + 1;
    while (close < fmt.size() && fmt[close] != '}') ++close;
    if (close == fmt.size()) break;  // незакрытая скобка: печатаем как есть
    sink.write(fmt.data() + run, i - run);
    char spec = close - i == 2 ? fmt[i + 1] : '\0';
    if (next < count) {
      write_arg(sink, args[next], spec);
    } else {
      sink.write("<?>", 3);
    }
    ++next;
    i = close + 1;
    run = i;
  }
  sink.write(fmt.data() + run, fmt.size() - run);
}

}  // namespace lib::core
