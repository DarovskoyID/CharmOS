#include "base/i18n/tag_converters.h"

#include <string>

#include "base/no_destructor.h"
#include "base/notreached.h"
#include "unicode/locid.h"

namespace base::i18n {
class LanguageTagConverter::Impl {};
LanguageTagConverter::LanguageTagConverter() = default;
LanguageTagConverter::~LanguageTagConverter() = default;
const LanguageTagConverter& LanguageTagConverter::GetInstance() {
  static base::NoDestructor<LanguageTagConverter> instance;
  return *instance;
}
std::optional<LanguageTag> LanguageTagConverter::FromString(std::string_view tag) const {
  if (tag.empty()) return std::nullopt;
  std::string input(tag);
  for (char& c : input) if (c == '_') c = '-';
  UErrorCode status = U_ZERO_ERROR;
  icu::Locale locale = icu::Locale::forLanguageTag(input,status);
  if (U_FAILURE(status)) return std::nullopt;
  std::string normalized = locale.toLanguageTag<std::string>(status);
  if (U_FAILURE(status) || normalized.empty() || (normalized == "und" && input != "und"))
    return std::nullopt;
  std::string_view part(normalized);
  return LanguageTag(LanguageTag::ImmutableStringType(base::span<const std::string_view>(&part, size_t{1})));
}
LanguageTag LanguageTagConverter::FromIcu4xCapiLocale(const icu4x::Locale& locale) const {
  NOTREACHED() << "ICU4X objects do not exist in the native ICU4C profile";
}
std::optional<LanguageTag> GetLanguageTagFromString(std::string_view tag) {
  return LanguageTagConverter::GetInstance().FromString(tag);
}
}  // namespace base::i18n
