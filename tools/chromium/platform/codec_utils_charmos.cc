// Native implementation of Chromium's codec utility API using upstream libpng.
#include "skia/ext/codec_utils.h"

#include "base/base64.h"
#include "skia/ext/skia_utils_base.h"
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/codec/SkPngDecoder.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/encode/SkPngEncoder.h"

namespace skia {
sk_sp<SkData> EncodePngAsSkData(const SkPixmap& src) {
  return SkPngEncoder::Encode(src,SkPngEncoder::Options{});
}
sk_sp<SkData> EncodePngAsSkData(GrDirectContext* context,const SkImage* src) {
  return SkPngEncoder::Encode(context,src,SkPngEncoder::Options{});
}
sk_sp<SkData> FastEncodePngAsSkData(GrDirectContext* context,const SkImage* src) {
  SkPngEncoder::Options options;
  options.fZLibLevel=1;
  return SkPngEncoder::Encode(context,src,options);
}
std::string EncodePngAsDataUri(const SkPixmap& src) {
  auto encoded=EncodePngAsSkData(src);
  if(!encoded) return {};
  return "data:image/png;base64,"+base::Base64Encode(as_byte_span(*encoded));
}
void EnsurePNGDecoderRegistered() {
  SkCodecs::Register(SkPngDecoder::Decoder());
}
}  // namespace skia
