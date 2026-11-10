// C++ implementations for the Rust-named Skia entry points Chromium calls.
//
// Chromium 155 routes PNG/JPEG/BMP/ICO decoding and PNG/JPEG encoding through
// Rust crates on supported platforms. CharmOS keeps the production binary
// Rust-free, so the same public entry points are provided here on top of
// Skia's native C++ codecs and FreeType typeface factory. Pixel output and
// option handling stay native; only the crate-backed code paths are replaced.
#include <memory>
#include <utility>

#include "include/codec/SkBmpDecoder.h"
#include "include/codec/SkIcoDecoder.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkTypeface.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/ports/SkTypeface_fontations.h"
#include "src/ports/SkTypeface_FreeType.h"
#include "third_party/skia/include/codec/SkBmpRustDecoder.h"
#include "third_party/skia/include/codec/SkPngRustDecoder.h"
#include "third_party/skia/include/encode/SkPngRustEncoder.h"
#include "third_party/skia/experimental/rust_ico/decoder/SkIcoRustDecoder.h"
#include "third_party/skia/experimental/rust_jpeg/decoder/SkJpegRustDecoder.h"
#include "third_party/skia/experimental/rust_jpeg/encoder/SkJpegRustEncoder.h"

namespace {

SkPngEncoder::Options ToNativePngOptions(
    const SkPngRustEncoder::Options& options) {
  SkPngEncoder::Options native;
  switch (options.fCompressionLevel) {
    case SkPngRustEncoder::CompressionLevel::kLow:
      native.fZLibLevel = 3;
      break;
    case SkPngRustEncoder::CompressionLevel::kMedium:
      native.fZLibLevel = 6;
      break;
    case SkPngRustEncoder::CompressionLevel::kHigh:
      native.fZLibLevel = 9;
      break;
  }
  native.fComments = options.fComments;
  native.fGainmap = options.fGainmap;
  native.fGainmapInfo = options.fGainmapInfo;
  return native;
}

std::unique_ptr<SkStreamAsset> MakeAssetFromData(sk_sp<const SkData> data) {
  if (!data) return nullptr;
  return std::make_unique<SkMemoryStream>(data->data(), data->size(),
                                          /*copyData=*/true);
}

}  // namespace

namespace SkPngRustDecoder {

bool IsPng(const void* data, size_t size) {
  return SkPngDecoder::IsPng(data, size);
}

std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream> stream,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkPngDecoder::Decode(std::move(stream), result, context);
}

}  // namespace SkPngRustDecoder

namespace SkPngRustEncoder {

bool Encode(SkWStream* dst, const SkPixmap& src, const Options& options) {
  return SkPngEncoder::Encode(dst, src, ToNativePngOptions(options));
}

sk_sp<SkData> Encode(const SkPixmap& src, const Options& options) {
  return SkPngEncoder::Encode(src, ToNativePngOptions(options));
}

sk_sp<SkData> Encode(GrDirectContext* ctx, const SkImage* img,
                     const Options& options) {
  return SkPngEncoder::Encode(ctx, img, ToNativePngOptions(options));
}

std::unique_ptr<SkEncoder> Make(SkWStream* dst, const SkPixmap& src,
                                const Options& options) {
  return SkPngEncoder::Make(dst, src, ToNativePngOptions(options));
}

}  // namespace SkPngRustEncoder

namespace SkJpegRustDecoder {

bool IsJpeg(const void* data, size_t size) {
  return SkJpegDecoder::IsJpeg(data, size);
}

std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream> stream,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkJpegDecoder::Decode(std::move(stream), result, context);
}

std::unique_ptr<SkCodec> Decode(sk_sp<const SkData> data,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkJpegDecoder::Decode(std::move(data), result, context);
}

}  // namespace SkJpegRustDecoder

namespace SkJpegRustEncoder {

bool Encode(SkWStream* dst, const SkPixmap& src,
            const SkJpegEncoder::Options& options) {
  return SkJpegEncoder::Encode(dst, src, options);
}

}  // namespace SkJpegRustEncoder

namespace SkBmpRustDecoder {

bool IsBmp(const void* data, size_t size) {
  return SkBmpDecoder::IsBmp(data, size);
}

std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream> stream,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkBmpDecoder::Decode(std::move(stream), result, context);
}

std::unique_ptr<SkCodec> Decode(sk_sp<const SkData> data,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkBmpDecoder::Decode(std::move(data), result, context);
}

}  // namespace SkBmpRustDecoder

namespace SkIcoRustDecoder {

bool IsIco(const void* data, size_t size) {
  return SkIcoDecoder::IsIco(data, size);
}

std::unique_ptr<SkCodec> Decode(std::unique_ptr<SkStream> stream,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkIcoDecoder::Decode(std::move(stream), result, context);
}

std::unique_ptr<SkCodec> Decode(sk_sp<const SkData> data,
                                SkCodec::Result* result,
                                SkCodecs::DecodeContext context) {
  return SkIcoDecoder::Decode(std::move(data), result, context);
}

}  // namespace SkIcoRustDecoder

// Fontations is Rust. CharmOS resolves font bytes with the FreeType typeface
// factory that backs the native directory font manager.
sk_sp<SkTypeface> SkTypeface_Make_Fontations(
    std::unique_ptr<SkStreamAsset> font_data,
    const SkFontArguments& args) {
  if (!font_data) return nullptr;
  return SkTypeface_FreeType::MakeFromStream(std::move(font_data), args);
}

sk_sp<SkTypeface> SkTypeface_Make_Fontations(sk_sp<const SkData> font_data,
                                             const SkFontArguments& args) {
  return SkTypeface_Make_Fontations(MakeAssetFromData(std::move(font_data)),
                                    args);
}

// Some callers only need an empty manager; the directory manager is installed
// as the process default by skia/ext/font_utils.cc.
sk_sp<SkFontMgr> SkFontMgr_New_Fontations_Empty() {
  return SkFontMgr::RefEmpty();
}
