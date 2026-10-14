#include <lib/manifest/manifest.hpp>

namespace lib::manifest {

using lib::core::Result;
using lib::core::StringView;

namespace {

using R = Result<Manifest, ParseError>;

class Parser {
 public:
  explicit Parser(StringView text) : text_(text) {}

  R run();

 private:
  bool at_end() const { return pos_ >= text_.size(); }
  char peek() const { return at_end() ? '\0' : text_[pos_]; }
  void skip_spaces() {
    while (!at_end() && (peek() == ' ' || peek() == '\t')) ++pos_;
  }
  // Пробелы и комментарий до конца строки; true — строка кончилась корректно.
  bool end_of_line() {
    skip_spaces();
    if (peek() == '#') {
      while (!at_end() && peek() != '\n') ++pos_;
    }
    if (at_end()) return true;
    if (peek() == '\r') ++pos_;
    if (peek() != '\n') return false;
    ++pos_;
    ++line_;
    return true;
  }
  bool word(StringView* out) {
    size_t start = pos_;
    while (!at_end()) {
      char c = peek();
      bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
      if (!ok) break;
      ++pos_;
    }
    *out = text_.substr(start, pos_ - start);
    return pos_ > start;
  }
  bool number(uint64_t* out) {
    uint64_t v = 0;
    size_t start = pos_;
    bool hex = text_.substr(pos_, 2) == StringView("0x");
    if (hex) pos_ += 2;
    size_t digits = 0;
    while (!at_end()) {
      char c = peek();
      uint64_t d;
      if (c >= '0' && c <= '9') {
        d = static_cast<uint64_t>(c - '0');
      } else if (hex && c >= 'a' && c <= 'f') {
        d = static_cast<uint64_t>(c - 'a' + 10);
      } else if (hex && c >= 'A' && c <= 'F') {
        d = static_cast<uint64_t>(c - 'A' + 10);
      } else {
        break;
      }
      uint64_t base = hex ? 16 : 10;
      if (v > (~uint64_t{0} - d) / base) return false;  // переполнение
      v = v * base + d;
      ++pos_;
      ++digits;
    }
    if (digits == 0) {
      pos_ = start;
      return false;
    }
    *out = v;
    return true;
  }
  R fail(const char* message) const { return R::err({line_, message}); }

  StringView text_;
  size_t pos_ = 0;
  uint32_t line_ = 1;
};

R Parser::run() {
  Manifest m = {};
  bool has_name = false;
  Match* current = nullptr;
  // Какие ключи уже были в текущей таблице (повтор — ошибка).
  bool seen_vendor = false, seen_device = false, seen_class = false, seen_score = false;

  while (!at_end()) {
    skip_spaces();
    if (end_of_line()) continue;  // пустая строка или комментарий

    if (peek() == '[') {
      if (text_.substr(pos_, 9) != StringView("[[match]]")) return fail("only [[match]] tables");
      pos_ += 9;
      if (!end_of_line()) return fail("garbage after [[match]]");
      if (m.match_count == kMaxMatches) return fail("too many [[match]] tables");
      current = &m.matches[m.match_count++];
      seen_vendor = seen_device = seen_class = seen_score = false;
      continue;
    }

    StringView key;
    if (!word(&key)) return fail("expected key");
    skip_spaces();
    if (peek() != '=') return fail("expected '='");
    ++pos_;
    skip_spaces();

    if (current == nullptr) {
      if (key != StringView("name")) return fail("unknown top-level key");
      if (has_name) return fail("duplicate key");
      if (peek() != '"') return fail("name must be a string");
      ++pos_;
      size_t n = 0;
      while (!at_end() && peek() != '"' && peek() != '\n') {
        char c = peek();
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return fail("name: only [a-z0-9_-]");
        if (n + 1 == kMaxName) return fail("name too long");
        m.name[n++] = c;
        ++pos_;
      }
      if (peek() != '"') return fail("unterminated string");
      ++pos_;
      if (n == 0) return fail("empty name");
      has_name = true;
    } else if (key == StringView("vendor") || key == StringView("class_code") ||
               key == StringView("score")) {
      bool& seen = key == StringView("vendor")       ? seen_vendor
                   : key == StringView("class_code") ? seen_class
                                                     : seen_score;
      if (seen) return fail("duplicate key");
      seen = true;
      uint64_t v;
      if (!number(&v)) return fail("expected number");
      if (key == StringView("vendor")) {
        if (v > 0xffff) return fail("vendor out of range");
        current->has_vendor = true;
        current->vendor = static_cast<uint16_t>(v);
      } else if (key == StringView("class_code")) {
        if (v > 0xffffff) return fail("class_code out of range");
        current->has_class = true;
        current->class_code = static_cast<uint32_t>(v);
      } else {
        if (v > kMaxScore) return fail("score out of range");
        current->score = static_cast<uint32_t>(v);
      }
    } else if (key == StringView("device")) {
      if (seen_device) return fail("duplicate key");
      seen_device = true;
      bool list = peek() == '[';
      if (list) ++pos_;
      for (;;) {
        skip_spaces();
        uint64_t v;
        if (!number(&v)) return fail("expected number");
        if (v > 0xffff) return fail("device out of range");
        if (current->device_count == kMaxDevices) return fail("too many devices");
        current->devices[current->device_count++] = static_cast<uint16_t>(v);
        if (!list) break;
        skip_spaces();
        if (peek() == ',') {
          ++pos_;
          continue;
        }
        if (peek() == ']') {
          ++pos_;
          break;
        }
        return fail("expected ',' or ']'");
      }
    } else {
      return fail("unknown key in [[match]]");
    }
    if (!end_of_line()) return fail("garbage after value");
  }
  if (!has_name) return fail("missing name");
  if (m.match_count == 0) return fail("no [[match]] table");
  for (uint32_t i = 0; i < m.match_count; ++i) {
    const Match& x = m.matches[i];
    if (!x.has_vendor && x.device_count == 0 && !x.has_class) {
      return fail("[[match]] without vendor, device or class_code");
    }
  }
  return R::ok(m);
}

}  // namespace

Result<Manifest, ParseError> parse(StringView text) { return Parser(text).run(); }

int64_t match_score(const Manifest& m, const DeviceId& id) {
  int64_t best = -1;
  for (uint32_t i = 0; i < m.match_count; ++i) {
    const Match& x = m.matches[i];
    if (x.has_vendor && x.vendor != id.vendor) continue;
    if (x.has_class && x.class_code != id.class_code) continue;
    if (x.device_count != 0) {
      bool any = false;
      for (uint32_t k = 0; k < x.device_count; ++k) any = any || x.devices[k] == id.device;
      if (!any) continue;
    }
    if (static_cast<int64_t>(x.score) > best) best = x.score;
  }
  return best;
}

}  // namespace lib::manifest
