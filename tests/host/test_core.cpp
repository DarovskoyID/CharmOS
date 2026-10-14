#include <lib/core/fmt.hpp>
#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

#include "test.hpp"

using lib::core::format_to;
using lib::core::Result;
using lib::core::StringView;

namespace {

bool eq(StringView got, const char* want) { return got == StringView(want); }

enum class Err : uint8_t { Oops = 3 };

}  // namespace

TEST(fmt_basic) {
  char buf[128];
  CHECK(eq(format_to(buf, sizeof buf, "plain"), "plain"));
  CHECK(eq(format_to(buf, sizeof buf, "a={} b={}", 1, 23u), "a=1 b=23"));
  CHECK(eq(format_to(buf, sizeof buf, "{}", "str"), "str"));
  CHECK(eq(format_to(buf, sizeof buf, "{} {}", true, false), "true false"));
  CHECK(eq(format_to(buf, sizeof buf, "[{}]", 'c'), "[c]"));
  CHECK(eq(format_to(buf, sizeof buf, "{}", StringView("view", 2)), "vi"));
}

TEST(fmt_signed) {
  char buf[64];
  CHECK(eq(format_to(buf, sizeof buf, "{}", -42), "-42"));
  CHECK(eq(format_to(buf, sizeof buf, "{}", int64_t{-9223372036854775807 - 1}),
           "-9223372036854775808"));
  CHECK(eq(format_to(buf, sizeof buf, "{}", uint64_t{18446744073709551615u}),
           "18446744073709551615"));
}

TEST(fmt_hex) {
  char buf[64];
  CHECK(eq(format_to(buf, sizeof buf, "{x}", 0), "0x0"));
  CHECK(eq(format_to(buf, sizeof buf, "{x}", 0xdeadu), "0xdead"));
  CHECK(eq(format_to(buf, sizeof buf, "{X}", 0xdeadu), "0x000000000000dead"));
  auto* p = reinterpret_cast<const void*>(uintptr_t{0x1000});
  CHECK(eq(format_to(buf, sizeof buf, "{}", p), "0x0000000000001000"));
  CHECK(eq(format_to(buf, sizeof buf, "{x}", p), "0x1000"));
  CHECK(eq(format_to(buf, sizeof buf, "{}", Err::Oops), "3"));
}

TEST(fmt_escapes_and_mismatch) {
  char buf[64];
  CHECK(eq(format_to(buf, sizeof buf, "{{}} {}", 5), "{} 5"));
  CHECK(eq(format_to(buf, sizeof buf, "{} {}", 1), "1 <?>"));
  CHECK(eq(format_to(buf, sizeof buf, "{}", 1, 2), "1"));
  CHECK(eq(format_to(buf, sizeof buf, "open {", 1), "open {"));
}

TEST(fmt_truncation) {
  char buf[6];
  lib::core::BufferSink sink(buf, sizeof buf);
  lib::core::format(sink, "{}", "abcdefgh");
  CHECK(eq(sink.view(), "abcde"));
  CHECK(sink.truncated());
  CHECK(buf[5] == '\0');
}

TEST(result_ok_and_err) {
  auto ok = Result<int, Err>::ok(7);
  CHECK(ok.is_ok() && ok.value() == 7);
  auto bad = Result<int, Err>::err(Err::Oops);
  CHECK(bad.is_err() && bad.error() == Err::Oops);
  CHECK(bad.value_or(1) == 1);
  auto st = lib::core::ok_status<Err>();
  CHECK(st.is_ok());
}

TEST(string_view_ops) {
  StringView s("ktest=pf rest");
  CHECK(s.size() == 13);
  CHECK(s.starts_with("ktest="));
  CHECK(!s.starts_with("ktest=pfx rest more"));
  CHECK(s.substr(6, 2) == StringView("pf"));
  CHECK(s.substr(100).empty());
}
