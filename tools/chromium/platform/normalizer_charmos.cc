#include "base/i18n/icubridge/normalizer.h"

#include "base/check.h"
#include "base/check_op.h"
#include "base/i18n/icubridge/icu_bridge_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/icu/source/common/unicode/normalizer2.h"
#include "third_party/icu/source/common/unicode/unistr.h"

namespace base::i18n {
namespace {
class Icu4cNormalizer final : public IcuBridge::Normalizer {
 public:
  std::u16string Normalize(NormalizationForm form,
                           std::u16string_view input) const override {
    UErrorCode status = U_ZERO_ERROR;
    const icu::Normalizer2* normalizer = nullptr;
    switch (form) {
      case NormalizationForm::NFC:
        normalizer = icu::Normalizer2::getNFCInstance(status);
        break;
      case NormalizationForm::NFD:
        normalizer = icu::Normalizer2::getNFDInstance(status);
        break;
      case NormalizationForm::NFKC:
        normalizer = icu::Normalizer2::getNFKCInstance(status);
        break;
    }
    CHECK(U_SUCCESS(status) && normalizer);
    CHECK_LE(input.size(),static_cast<size_t>(INT32_MAX));
    icu::UnicodeString source(input.data(),static_cast<int32_t>(input.size()));
    icu::UnicodeString output;
    normalizer->normalize(source,output,status);
    CHECK(U_SUCCESS(status));
    return UnicodeStringToString16(output);
  }
};
}
std::unique_ptr<IcuBridge::Normalizer> CreateIcu4xNormalizer(
    base::PassKey<IcuBridge> key) {
  return nullptr;
}
std::unique_ptr<IcuBridge::Normalizer> CreateIcu4cNormalizer(
    base::PassKey<IcuBridge> key) {
  return std::make_unique<Icu4cNormalizer>();
}
}  // namespace base::i18n
