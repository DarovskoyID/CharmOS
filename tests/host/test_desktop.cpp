#include "test.hpp"
#include <charm/expression.hpp>
#include <charm/text_input.hpp>
#include <charm/units.hpp>
#include <charm/preferences.hpp>
#include <charm/text_buffer.hpp>
#include <charm/calculator_profiles.hpp>
using charm::math::evaluate;
using charm::math::Error;
TEST(scientific_functions_exponents_and_extreme_roots) {
  auto close=[](double a,double b,double eps) { return a>b-eps && a<b+eps; };
  CHECK(close(evaluate("ln(exp(2))").value,2,1e-12));
  CHECK(close(evaluate("log(1000)").value,3,1e-12));
  CHECK(close(evaluate("tan(pi/4)").value,1,1e-12));
  CHECK(close(evaluate("sqrt(1e-200)").value,1e-100,1e-112));
  CHECK(close(evaluate("1.25e3+2E-1").value,1250.2,1e-9));
  CHECK(evaluate("ln(0)").error==Error::Domain);
  CHECK(evaluate("tan(pi/2)").error==Error::Domain);
  CHECK(evaluate("exp(1000)").error==Error::Limit);
  CHECK(evaluate("1e+").error==Error::Syntax);
  CHECK(evaluate("sqrt(x)",__builtin_inf()).error==Error::Limit);
  CHECK(evaluate("exp(x)",__builtin_nan("")).error==Error::Limit);
}
TEST(programmer_precedence_modulo_and_invalid_operations) {
  using charm::calc::integer;
  CHECK(integer("0xFF & 0b1010").value==10);
  CHECK(integer("1 << 4 + 1").value==32);
  CHECK(integer("1 | 2 ^ 3 & 4").value==3);
  CHECK(integer("~0").value==UINT64_MAX);
  CHECK(integer("18446744073709551615+1").value==0);
  CHECK(integer("(12+3)*2%7").value==2);
  CHECK(integer("-1>>63").value==1);
  const char* bad[]={"","0x","0b2","2/0","1%0","1<<64","1>>-1","0x10000000000000000","18446744073709551616","1.5","(1+2","2&&3"};
  for(auto* text:bad) CHECK(!integer(text).valid);
  char text[65]; charm::calc::base_text(UINT64_MAX,2,text,sizeof text); CHECK(strlen(text)==64);
  charm::calc::base_text(255,16,text,sizeof text); CHECK_STR(text,strlen(text),"FF");
  char small[2]; charm::calc::base_text(255,16,small,sizeof small); CHECK(small[0]==0);
}
TEST(chemistry_group_counts_unknown_elements_and_limits) {
  using charm::calc::molar_mass;
  auto water=molar_mass("H2O"); CHECK(water.valid); CHECK(water.mass>18.0149 && water.mass<18.0151);
  auto lime=molar_mass("Ca(OH)2"); CHECK(lime.valid); CHECK(lime.mass>74.091 && lime.mass<74.093);
  auto alum=molar_mass("Al2(SO4)3"); CHECK(alum.valid); CHECK(alum.mass>342.12 && alum.mass<342.14);
  const char* bad[]={"","()","H0","2H2O","Xx2","Ca(OH","H2O)","H10000","((((((((((H))))))))))"};
  for(auto* text:bad) CHECK(!molar_mass(text).valid);
}
TEST(resistor_and_spring_reference_models_and_invalid_parameters) {
  using namespace charm::calc;
  auto series=resistors(12,100,200,false); CHECK(series.valid); CHECK(series.resistance==300); CHECK(series.current==.04); CHECK(series.power==.48);
  auto parallel=resistors(12,100,200,true); CHECK(parallel.valid); CHECK(parallel.resistance>66.666 && parallel.resistance<66.667); CHECK(parallel.current>.17999 && parallel.current<.18001);
  CHECK(parallel.branch1+parallel.branch2>.17999 && parallel.branch1+parallel.branch2<.18001);
  CHECK(!resistors(12,0,100,true).valid); CHECK(!resistors(-1,100,100,false).valid);
  auto model=spring(10,100,1); CHECK(model.valid); CHECK(model.displacement==.1); CHECK(model.period>.62831 && model.period<.62832); CHECK(model.energy>.49999 && model.energy<.50001);
  CHECK(!spring(10,0,1).valid); CHECK(!spring(10,100,-1).valid);
  auto tiny=spring(0,1,1e-100); CHECK(tiny.valid); CHECK(tiny.period>6.28318e-50 && tiny.period<6.28319e-50);
  CHECK(!resistors(1,1e-300,1,true).valid);
  CHECK(!spring(1,1e-300,1).valid);
}
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
