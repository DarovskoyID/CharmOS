#include "components/qr_code_generator/qr_code_generator.h"

#include <utility>

#include "base/no_destructor.h"
#include "fxbarcode/common/BC_CommonByteMatrix.h"
#include "fxbarcode/qrcode/BC_QRCoder.h"
#include "fxbarcode/qrcode/BC_QRCoderEncoder.h"
#include "fxbarcode/qrcode/BC_QRCoderErrorCorrectionLevel.h"

namespace qr_code_generator {
namespace {

struct QrRuntime {
  QrRuntime() { CBC_QRCoderEncoder::Initialize(); }
  ~QrRuntime() { CBC_QRCoderEncoder::Finalize(); }
};

}  // namespace

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
  static base::NoDestructor<QrRuntime> runtime;
  ByteString input(reinterpret_cast<const char*>(in.data()), in.size());
  CBC_QRCoder qr;
  if (!CBC_QRCoderEncoder::EncodeBytesWithMinimumVersion(
          input, CBC_QRCoderErrorCorrectionLevel::M, &qr, version)) {
    return base::unexpected(Error::kInputTooLong);
  }
  std::unique_ptr<CBC_CommonByteMatrix> matrix = qr.TakeMatrix();
  GeneratedCode code;
  code.qr_size = qr.GetMatrixWidth();
  code.data = matrix->TakeArray();
  return code;
}

}  // namespace qr_code_generator
