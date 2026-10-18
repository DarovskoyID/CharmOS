// Copyright (c) 2026 CharmOS contributors. MIT license.
#include "base/json/json_reader.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "build/charmos/json_parser.h"

namespace {
struct ChromiumValuePolicy {
  using Value = base::Value;
  static Value Null() { return Value(); }
  static Value Bool(bool value) { return Value(value); }
  static Value String(std::string value) { return Value(std::move(value)); }
  static Value List() { return Value(base::ListValue()); }
  static Value Dict() { return Value(base::DictValue()); }
  static void Append(Value& list, Value value) {
    list.GetList().Append(std::move(value));
  }
  static void Set(Value& dict, std::string key, Value value) {
    dict.GetDict().Set(key, std::move(value));
  }
  static std::optional<Value> Number(std::string_view text) {
    if (text == "-0") return Value(-0.0);
    int integer;
    if (base::StringToInt(text, &integer)) return Value(integer);
    double number;
    if (!base::StringToDouble(text, &number) || !std::isfinite(number))
      return std::nullopt;
    return Value(number);
  }
};
base::JSONReader::Result Decode(std::string_view input, int options, size_t depth) {
  charmos_json::Parser<ChromiumValuePolicy> parser(
      input, options, std::min(depth, base::internal::kAbsoluteMaxDepth));
  auto result = parser.Parse();
  if (result) return std::move(*result);
  const auto& error = parser.error();
  return base::unexpected(base::JSONReader::Error{
      .message = error.message, .line = error.line, .column = error.column});
}
} // namespace

namespace base {
std::string JSONReader::Error::ToString() const {
  return StrCat({"line ", NumberToString(line), ", column ",
                 NumberToString(column), ": ", message});
}
std::optional<Value> JSONReader::Read(std::string_view json, int options,
                                      size_t max_depth) {
  auto result = Decode(json, options, max_depth);
  if (!result.has_value()) return std::nullopt;
  return std::move(*result);
}
std::optional<DictValue> JSONReader::ReadDict(std::string_view json, int options,
                                              size_t max_depth) {
  auto value = Read(json, options, max_depth);
  if (!value || !value->is_dict()) return std::nullopt;
  return std::move(*value).TakeDict();
}
std::optional<ListValue> JSONReader::ReadList(std::string_view json, int options,
                                              size_t max_depth) {
  auto value = Read(json, options, max_depth);
  if (!value || !value->is_list()) return std::nullopt;
  return std::move(*value).TakeList();
}
JSONReader::Result JSONReader::ReadAndReturnValueWithError(std::string_view json,
                                                           int options) {
  return Decode(json, options, internal::kAbsoluteMaxDepth);
}
} // namespace base
