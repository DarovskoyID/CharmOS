#ifndef BUILD_CHARMOS_QR_CODE_PDFIUM_CHARMOS_H_
#define BUILD_CHARMOS_QR_CODE_PDFIUM_CHARMOS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace qr_code_generator::charmos {

struct NativeQrCode {
  std::vector<uint8_t> data;
  int size;
};

std::optional<NativeQrCode> GenerateNativeQrCode(const uint8_t* input,
                                                 size_t input_size,
                                                 int minimum_version);

}  // namespace qr_code_generator::charmos

#endif  // BUILD_CHARMOS_QR_CODE_PDFIUM_CHARMOS_H_
