#if defined(__charmos__)
#include "build/charmos/json_parser.h"
#else
#include "../../tools/chromium/platform/json_parser.h"
#endif
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <variant>
#include <vector>

struct Node {
  std::variant<std::nullptr_t, bool, double, std::string,
               std::vector<Node>, std::map<std::string, Node>> data;
};
struct Policy {
  using Value = Node;
  static Node Null() { return {nullptr}; }
  static Node Bool(bool b) { return {b}; }
  static Node String(std::string s) { return {std::move(s)}; }
  static Node List() { return {std::vector<Node>{}}; }
  static Node Dict() { return {std::map<std::string, Node>{}}; }
  static void Append(Node& n, Node value) {
    std::get<std::vector<Node>>(n.data).push_back(std::move(value));
  }
  static void Set(Node& n, std::string key, Node value) {
    std::get<std::map<std::string, Node>>(n.data).insert_or_assign(std::move(key), std::move(value));
  }
  static std::optional<Node> Number(std::string_view s) {
    std::string text(s);
    char* end;
    double v = strtod(text.c_str(), &end);
    if (!std::isfinite(v) || end != text.c_str() + text.size()) return std::nullopt;
    return Node{v};
  }
};
static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static std::optional<Node> Parse(std::string_view s, int options = 0, size_t depth = 200) {
  charmos_json::Parser<Policy> parser(s, options, depth);
  auto result = parser.Parse();
  if (!result) CHECK(!parser.error().message.empty());
  return result;
}
#if defined(__charmos__)
extern "C"
#endif
int main() {
  for (auto s : {"null", "true", "false", "0", "-0", "1.25e-3", "2147483648",
                 "[]", "{}", "[1,true,null,{},[]]", "\"Привет\"", "\"\\uD83D\\uDE00\""})
    CHECK(Parse(s));
  for (auto s : {"", " ", "nul", "True", "01", "+1", "-", "1.", ".1", "1e", "1e+", "1e999",
                 "NaN", "Infinity", "[1,]", "{\"a\":1,}", "{a:1}", "{\"a\" 1}", "[1 2]",
                 "[", "{", "\"", "\"\\", "\"\\q\"", "\"\\u12xx\"", "\"\\uD800\"",
                 "\"\\uDC00\"", "\"a\nb\"", "true false", "//x\nnull", "/*x*/null"}) CHECK(!Parse(s));
  auto duplicate = Parse("{\"x\":1,\"x\":2}");
  CHECK(duplicate && std::get<double>(std::get<std::map<std::string, Node>>(duplicate->data).at("x").data) == 2);
  auto unicode = Parse("\"\\uD83D\\uDE00 / \\xFF\"", charmos_json::HexEscape);
  CHECK(unicode && std::get<std::string>(unicode->data) == "😀 / ÿ");
  auto repaired = Parse("\"\\uD800a\\uDC00\"", charmos_json::ReplaceInvalid);
  CHECK(repaired && std::get<std::string>(repaired->data) == "�a�");
  CHECK(Parse("\xef\xbb\xbf{}"));
  CHECK(Parse("/*a*/[1,//b\n2,]", charmos_json::Comments | charmos_json::TrailingCommas));
  CHECK(!Parse("/*a", charmos_json::Comments));
  CHECK(Parse("\"a\nb\r\"", charmos_json::Newlines));
  CHECK(!Parse(std::string("\"a\0b\"", 5), charmos_json::Newlines));
  CHECK(Parse("\"\\v\"", charmos_json::VerticalTab));
  CHECK(!Parse("\"\\v\"", charmos_json::HexEscape));
  CHECK(!Parse("\"\\xFF\"", charmos_json::VerticalTab));
  CHECK(Parse("0", 0, 1));
  CHECK(!Parse("0", 0, 0));
  CHECK(Parse("[]", 0, 1));
  CHECK(!Parse("[0]", 0, 1));
  CHECK(Parse("[0]", 0, 2));
  for (std::string bad : {std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
                          std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)}) {
    CHECK(!Parse("\"" + bad + "\""));
    CHECK(Parse("\"" + bad + "\"", charmos_json::ReplaceInvalid));
  }
  std::string nested(200, '['); nested += '0'; nested += std::string(200, ']');
  CHECK(!Parse(nested));
  CHECK(Parse(nested, 0, 201));
  std::string valid = "{\"a\":[true,null,\"\\uD83D\\uDE00\",1.2e3]}";
  for (size_t i = 0; i < valid.size(); ++i) CHECK(!Parse(std::string_view(valid).substr(0, i)));
  charmos_json::Parser<Policy> located("{\n\"a\":?}", 0, 200);
  CHECK(!located.Parse());
  CHECK(located.error().line == 2 && located.error().column == 5);
  std::printf("Chromium C++ JSON: %s\n", failures ? "FAILED" : "passed");
  return failures ? 1 : 0;
}
