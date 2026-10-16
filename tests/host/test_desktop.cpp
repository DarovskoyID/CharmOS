#include "test.hpp"
#include <charm/expression.hpp>
#include <charm/text_input.hpp>
#include <charm/units.hpp>
#include <charm/preferences.hpp>
#include <charm/text_buffer.hpp>
using charm::math::evaluate;
using charm::math::Error;
TEST(expressions_precedence_domains_and_limits) {
  CHECK(evaluate("5*3-2").value == 13);
  CHECK(evaluate("-2^2").value == -4);
  CHECK(evaluate("2^3^2").value == 512);
  CHECK(evaluate("2^-3").value == .125);
  CHECK(evaluate("sqrt(81)+abs(-2)").value == 11);
  CHECK(evaluate("1/0").error == Error::DivisionByZero);
  CHECK(evaluate("sqrt(-1)").error == Error::Domain);
  CHECK(evaluate("2^0.5").error == Error::Domain);
  CHECK(evaluate("1 + garbage").error == Error::Syntax);
  CHECK(evaluate("1 2").error == Error::Syntax);
  CHECK(evaluate("2^99999").error == Error::Limit);
  CHECK(evaluate("((((((((((((((((((((((((((((((((((((1").error == Error::Limit);
  for (int x = -20; x <= 20; ++x) {
    CHECK(evaluate("x*x+2*x+1", x).value == (x + 1) * (x + 1));
    double identity = evaluate("sin(x)^2+cos(x)^2", x).value;
    CHECK(identity > .999999999 && identity < 1.000000001);
  }
}
TEST(russian_input_and_utf8_bounds) {
  char text[8]{}; unsigned n = 0;
  CHECK(charm::ui::append(text, sizeof text, n, charm::ui::key(4, false, true)));
  CHECK(charm::ui::append(text, sizeof text, n, charm::ui::key(5, true, true)));
  CHECK_STR(text, n, "фИ");
  charm::ui::backspace(text, n); CHECK_STR(text, n, "ф");
  charm::ui::backspace(text, n); CHECK(n == 0);
  CHECK(!charm::ui::append(text, 2, n, 0x444)); CHECK(n == 0);
  CHECK(!charm::ui::append(text, sizeof text, n, 0xd800));
  CHECK(charm::ui::key(0x25, true, false) == '*');
}
TEST(units_dimensions_temperature_and_query_validation) {
  using charm::math::convert; using charm::math::convert_query;
  CHECK(convert(10,"m","cm").value == 1000);
  CHECK(convert_query("10 м -> см").value == 1000);
  CHECK(convert_query("2 h min").value == 120);
  CHECK(!convert_query("2 m s").valid);
  CHECK(!convert_query("2 m cm garbage").valid);
  CHECK(!convert_query("2 m").valid);
  CHECK(!convert(-300,"C","K").valid);
  auto c = convert(32,"F","C"); CHECK(c.valid && c.value > -.000001 && c.value < .000001);
  CHECK(evaluate("a*x", 3, 2).value == 6);
}
TEST(preferences_detect_corruption_and_unknown_versions) {
  auto p=charm::ui::preferences(42,3); CHECK(charm::ui::valid(p));
  auto* bytes=reinterpret_cast<unsigned char*>(&p);
  for (unsigned i=0;i<sizeof p;++i) { bytes[i]^=1; CHECK(!charm::ui::valid(p)); bytes[i]^=1; }
  CHECK(!charm::ui::valid(charm::ui::preferences(1,8)));
  CHECK(!charm::ui::valid(charm::ui::preferences(0,3)));
}
TEST(editor_unicode_insert_delete_capacity_and_undo) {
  charm::ui::TextBuffer<12> buffer;
  CHECK(buffer.insert(0x444)); CHECK(buffer.insert('x'));
  buffer.left(); CHECK(buffer.cursor==2); CHECK(buffer.insert(0x438));
  CHECK_STR(buffer.text,buffer.length,"фиx");
  CHECK(buffer.erase()); CHECK_STR(buffer.text,buffer.length,"фx");
  CHECK(buffer.undo()); CHECK_STR(buffer.text,buffer.length,"фиx");
  CHECK(buffer.undo()); CHECK_STR(buffer.text,buffer.length,"фx");
  CHECK(buffer.insert('a',true)); CHECK_STR(buffer.text,buffer.length,"a");
  CHECK(buffer.undo()); CHECK_STR(buffer.text,buffer.length,"фx");
  CHECK(buffer.insert('\n')); CHECK(buffer.line_start()==buffer.cursor);
  for(int i=0;i<20;++i) buffer.insert('b');
  CHECK(buffer.length<sizeof buffer.text); CHECK(buffer.text[buffer.length]==0);
}
