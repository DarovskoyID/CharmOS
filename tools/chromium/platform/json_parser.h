// Copyright (c) 2026 CharmOS contributors. MIT license.
#ifndef CHARMOS_CHROMIUM_JSON_PARSER_H_
#define CHARMOS_CHROMIUM_JSON_PARSER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace charmos_json {
// Bit values are the public Chromium JSONParserOptions contract.
enum Option {
  TrailingCommas = 1, ReplaceInvalid = 2, Comments = 4,
  VerticalTab = 8, HexEscape = 16, Newlines = 32,
};
struct Error { std::string message; int line = 1; int column = 1; };

// Policy creates the caller's actual value type; no intermediate JSON tree.
template<class Policy> class Parser {
 public:
  using Value = typename Policy::Value;
  Parser(std::string_view input, int options, size_t max_depth)
      : input_(input), options_(options), max_depth_(max_depth) {}

  std::optional<Value> Parse() {
    if (input_.starts_with("\xef\xbb\xbf")) pos_ = 3;
    auto value = Read(0);
    if (!value || !Space()) return std::nullopt;
    if (pos_ != input_.size()) return Fail("Unexpected trailing input");
    return value;
  }
  const Error& error() const { return error_; }

 private:
  std::optional<Value> Fail(const char* message) {
    Mark(message);
    return std::nullopt;
  }
  bool Mark(const char* message) {
    if (error_.message.empty()) {
      error_.message = message;
      for (size_t i = 0; i < pos_; ++i) {
        if (input_[i] == '\n') { ++error_.line; error_.column = 1; }
        else { ++error_.column; }
      }
    }
    return false;
  }
  bool Take(char c) {
    if (pos_ < input_.size() && input_[pos_] == c) { ++pos_; return true; }
    return false;
  }
  bool Space() {
    for (;;) {
      while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\r' ||
             input_[pos_] == '\n' || input_[pos_] == '\t')) ++pos_;
      if (!(options_ & Comments) || pos_ + 1 >= input_.size() || input_[pos_] != '/') return true;
      if (input_[pos_ + 1] == '/') {
        pos_ += 2;
        while (pos_ < input_.size() && input_[pos_] != '\n' && input_[pos_] != '\r') ++pos_;
      } else if (input_[pos_ + 1] == '*') {
        pos_ += 2;
        while (pos_ + 1 < input_.size() && input_.substr(pos_, 2) != "*/") ++pos_;
        if (pos_ + 1 >= input_.size()) return Mark("Unterminated comment");
        pos_ += 2;
      } else return true;
    }
  }
  static void Utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xc0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xe0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 63));
      out += static_cast<char>(0x80 | (cp & 63));
    } else {
      out += static_cast<char>(0xf0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 63));
      out += static_cast<char>(0x80 | ((cp >> 6) & 63));
      out += static_cast<char>(0x80 | (cp & 63));
    }
  }
  static int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  bool HexCode(size_t count, uint32_t& cp) {
    cp = 0;
    for (size_t i = 0; i < count; ++i) {
      if (pos_ == input_.size() || Hex(input_[pos_]) < 0) return Mark("Invalid hexadecimal escape");
      cp = (cp << 4) | static_cast<uint32_t>(Hex(input_[pos_++]));
    }
    return true;
  }
  bool Invalid(std::string& out) {
    if (!(options_ & ReplaceInvalid)) return Mark("Invalid Unicode character");
    Utf8(out, 0xfffd);
    return true;
  }
  bool RawUtf8(std::string& out) {
    const size_t start = pos_;
    auto first = static_cast<unsigned char>(input_[pos_++]);
    size_t count = first >= 0xc2 && first <= 0xdf ? 2 :
                   first >= 0xe0 && first <= 0xef ? 3 :
                   first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!count) return Invalid(out);
    uint32_t cp = first & ((1u << (7 - count)) - 1);
    for (size_t i = 1; i < count; ++i) {
      if (pos_ == input_.size()) return Invalid(out);
      const auto next = static_cast<unsigned char>(input_[pos_]);
      if ((next & 0xc0) != 0x80) return Invalid(out);
      // Reject forbidden second bytes before consuming the continuation.
      if (i == 1 && ((first == 0xe0 && next < 0xa0) ||
          (first == 0xed && next >= 0xa0) || (first == 0xf0 && next < 0x90) ||
          (first == 0xf4 && next >= 0x90))) return Invalid(out);
      cp = (cp << 6) | (next & 63);
      ++pos_;
    }
    out.append(input_.substr(start, pos_ - start));
    return true;
  }
  bool String(std::string& out) {
    if (!Take('"')) return Mark("Expected string");
    while (pos_ < input_.size()) {
      auto c = static_cast<unsigned char>(input_[pos_]);
      if (c >= 0x80) { if (!RawUtf8(out)) return false; continue; }
      ++pos_;
      if (c == '"') return true;
      if (c < 0x20 && !((options_ & Newlines) && (c == '\n' || c == '\r')))
        return Mark("Unescaped control character");
      if (c != '\\') { out += static_cast<char>(c); continue; }
      if (pos_ == input_.size()) return Mark("Unterminated escape");
      c = static_cast<unsigned char>(input_[pos_++]);
      switch (c) {
        case '"': case '\\': case '/': out += static_cast<char>(c); break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'v':
          if (!(options_ & VerticalTab)) return Mark("Vertical-tab escape disabled");
          out += '\v'; break;
        case 'x': case 'u': {
          if (c == 'x' && !(options_ & HexEscape)) return Mark("Hex escape disabled");
          uint32_t cp;
          if (!HexCode(c == 'x' ? 2 : 4, cp)) return false;
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (input_.substr(pos_, 2) == "\\u" && pos_ + 6 <= input_.size()) {
              uint32_t low = 0;
              bool hex = true;
              for (size_t i = pos_ + 2; i < pos_ + 6; ++i) {
                int digit = Hex(input_[i]);
                if (digit < 0) { hex = false; break; }
                low = (low << 4) | static_cast<uint32_t>(digit);
              }
              if (hex && low >= 0xdc00 && low <= 0xdfff) {
                pos_ += 6;
                Utf8(out, 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00);
                break;
              }
            }
            if (!Invalid(out)) return false;
          } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            if (!Invalid(out)) return false;
          } else Utf8(out, cp);
          break;
        }
        default: return Mark("Invalid escape");
      }
    }
    return Mark("Unterminated string");
  }
  std::optional<Value> Number() {
    size_t start = pos_;
    Take('-');
    if (!Take('0')) {
      if (pos_ == input_.size() || input_[pos_] < '1' || input_[pos_] > '9')
        return Fail("Invalid number");
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
    }
    for (char marker : {'.', 'e'}) {
      bool present = marker == '.' ? Take('.') : (Take('e') || Take('E'));
      if (!present) continue;
      if (marker == 'e') { if (!Take('+')) Take('-'); }
      size_t digits = pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
      if (digits == pos_) return Fail("Expected number digits");
    }
    auto value = Policy::Number(input_.substr(start, pos_ - start));
    if (!value) return Fail("Number out of range");
    return value;
  }
  std::optional<Value> Read(size_t depth) {
    if (!Space()) return std::nullopt;
    if (pos_ == input_.size()) return Fail("Expected value");
    if (depth >= max_depth_) return Fail("Maximum JSON depth exceeded");
    char c = input_[pos_];
    if (c == '"') {
      std::string out;
      if (!String(out)) return std::nullopt;
      return Policy::String(std::move(out));
    }
    if (c == '[' || c == '{') {
      ++pos_;
      bool dict = c == '{';
      char close = dict ? '}' : ']';
      Value result = dict ? Policy::Dict() : Policy::List();
      if (!Space()) return std::nullopt;
      if (Take(close)) return result;
      for (;;) {
        std::string key;
        if (dict) {
          if (!String(key) || !Space()) return std::nullopt;
          if (!Take(':')) return Fail("Expected colon");
        }
        auto value = Read(depth + 1);
        if (!value) return std::nullopt;
        if (dict) Policy::Set(result, std::move(key), std::move(*value));
        else Policy::Append(result, std::move(*value));
        if (!Space()) return std::nullopt;
        if (Take(close)) return result;
        if (!Take(',')) return Fail("Expected comma or closing delimiter");
        if (!Space()) return std::nullopt;
        if ((options_ & TrailingCommas) && Take(close)) return result;
      }
    }
    for (auto token : {std::string_view("null"), std::string_view("true"), std::string_view("false")}) {
      if (input_.substr(pos_, token.size()) == token) {
        pos_ += token.size();
        return token == "null" ? Policy::Null() : Policy::Bool(token == "true");
      }
    }
    if (c == '-' || (c >= '0' && c <= '9')) return Number();
    return Fail("Unexpected token");
  }
  std::string_view input_;
  int options_;
  size_t max_depth_;
  size_t pos_ = 0;
  Error error_;
};
} // namespace charmos_json
#endif
