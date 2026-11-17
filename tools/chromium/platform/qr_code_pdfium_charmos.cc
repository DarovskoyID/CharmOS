#include "build/charmos/qr_code_pdfium_charmos.h"

#include <utility>

#include "core/fxcrt/bytestring.h"
#include "fxbarcode/common/BC_CommonByteMatrix.h"
#include "fxbarcode/qrcode/BC_QRCoder.h"
#include "fxbarcode/qrcode/BC_QRCoderEncoder.h"
#include "fxbarcode/qrcode/BC_QRCoderErrorCorrectionLevel.h"

namespace qr_code_generator::charmos {
namespace {

struct QrRuntime {
  QrRuntime() { CBC_QRCoderEncoder::Initialize(); }
  ~QrRuntime() { CBC_QRCoderEncoder::Finalize(); }
};

}  // namespace

std::optional<NativeQrCode> GenerateNativeQrCode(const uint8_t* input,
                                                 size_t input_size,
                                                 int minimum_version) {
  static auto* runtime = new QrRuntime;
  (void)runtime;
  ByteString bytes(reinterpret_cast<const char*>(input), input_size);
  CBC_QRCoder qr;
  if (!CBC_QRCoderEncoder::EncodeBytesWithMinimumVersion(
          bytes, CBC_QRCoderErrorCorrectionLevel::M, &qr, minimum_version)) {
    return std::nullopt;
  }
  std::unique_ptr<CBC_CommonByteMatrix> matrix = qr.TakeMatrix();
  auto data = matrix->TakeArray();
  NativeQrCode code;
  code.size = qr.GetMatrixWidth();
  code.data.assign(data.begin(), data.end());
  return code;
}

}  // namespace qr_code_generator::charmos
