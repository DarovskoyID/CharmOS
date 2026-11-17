#include "components/qr_code_generator/qr_code_generator.h"

#include <utility>

#include "build/charmos/qr_code_pdfium_charmos.h"

namespace qr_code_generator {

GeneratedCode::GeneratedCode() = default;
GeneratedCode::~GeneratedCode() = default;
GeneratedCode::GeneratedCode(GeneratedCode&&) = default;
GeneratedCode& GeneratedCode::operator=(GeneratedCode&&) = default;

base::expected<GeneratedCode, Error> GenerateCode(
    base::span<const uint8_t> in,
    std::optional<int> min_version) {
  const int version = min_version.value_or(1);
  if (version < 1 || version > 40) {
    return base::unexpected(Error::kUnknownError);
  }
  auto native = charmos::GenerateNativeQrCode(in.data(), in.size(), version);
  if (!native) {
    return base::unexpected(Error::kInputTooLong);
  }
  GeneratedCode code;
  code.qr_size = native->size;
  code.data = std::move(native->data);
  return code;
}

}  // namespace qr_code_generator
