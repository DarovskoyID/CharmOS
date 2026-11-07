#include "base/i18n/language_tag_matcher.h"

#include <vector>

#include "base/check.h"
#include "base/i18n/tag_converters.h"
#include "unicode/localematcher.h"
#include "unicode/locid.h"

namespace base::i18n {
LanguageTagMatcher::LanguageTagMatcher(
    base::flat_map<LanguageTag,LanguageTag> closest_supported_tag,
    std::unique_ptr<icu::LocaleMatcher> matcher)
    : closest_supported_tag_(std::move(closest_supported_tag)),
      icu_fallbacker_(std::move(matcher)) {}
LanguageTagMatcher::~LanguageTagMatcher() = default;
LanguageTagMatcher::LanguageTagMatcher(LanguageTagMatcher&&) noexcept = default;
LanguageTagMatcher& LanguageTagMatcher::operator=(LanguageTagMatcher&&) noexcept = default;
LanguageTagMatcher LanguageTagMatcher::Create(base::span<const LanguageTag> supported_tags) {
  base::flat_map<LanguageTag,LanguageTag> supported;
  std::vector<icu::Locale> locales;
  for (const auto& tag : supported_tags) {
    supported.insert({tag,tag});
    UErrorCode status = U_ZERO_ERROR;
    locales.push_back(icu::Locale::forLanguageTag(tag.tag_string(),status));
    CHECK(U_SUCCESS(status));
  }
  UErrorCode status = U_ZERO_ERROR;
  auto builder = icu::LocaleMatcher::Builder();
  builder.setSupportedLocales(locales.begin(),locales.end()).setNoDefaultLocale();
  auto matcher = std::make_unique<icu::LocaleMatcher>(builder.build(status));
  CHECK(U_SUCCESS(status));
  return LanguageTagMatcher(std::move(supported),std::move(matcher));
}
std::optional<LanguageTag> LanguageTagMatcher::Match(const LanguageTag& preferred_tag) const {
  auto exact = closest_supported_tag_.find(preferred_tag);
  if (exact != closest_supported_tag_.end()) return exact->second;
  UErrorCode status = U_ZERO_ERROR;
  auto desired = icu::Locale::forLanguageTag(preferred_tag.tag_string(),status);
  if (U_FAILURE(status)) return std::nullopt;
  const icu::Locale* matched = icu_fallbacker_->getBestMatch(desired,status);
  if (U_FAILURE(status) || !matched) return std::nullopt;
  std::string tag = matched->toLanguageTag<std::string>(status);
  if (U_FAILURE(status)) return std::nullopt;
  return LanguageTagConverter::GetInstance().FromString(tag);
}
bool LanguageTagMatcher::HasExactMatch(const LanguageTag& preferred_tag) const {
  return closest_supported_tag_.contains(preferred_tag);
}
LanguageTagMatcherWithDefault::LanguageTagMatcherWithDefault(
    LanguageTag default_tag,LanguageTagMatcher matcher)
    : default_tag_(std::move(default_tag)),matcher_(std::move(matcher)) {}
LanguageTagMatcherWithDefault LanguageTagMatcherWithDefault::Create(
    LanguageTag default_tag,base::span<const LanguageTag> supported_tags) {
  return LanguageTagMatcherWithDefault(std::move(default_tag),
                                      LanguageTagMatcher::Create(supported_tags));
}
std::optional<LanguageTag> LanguageTagMatcherWithDefault::Match(
    const LanguageTag& preferred_tag) const {
  return matcher_.Match(preferred_tag);
}
bool LanguageTagMatcherWithDefault::HasExactMatch(const LanguageTag& preferred_tag) const {
  return matcher_.HasExactMatch(preferred_tag);
}
LanguageTag LanguageTagMatcherWithDefault::MatchOrDefault(
    const LanguageTag& preferred_tag) const {
  auto matched = Match(preferred_tag);
  return matched ? *matched : default_tag_;
}
}  // namespace base::i18n
