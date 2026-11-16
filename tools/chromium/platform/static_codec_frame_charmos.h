#ifndef CHARMOS_STATIC_CODEC_FRAME_H_
#define CHARMOS_STATIC_CODEC_FRAME_H_
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/codec/SkCodecAnimation.h"
namespace charmos {
// Static native codecs may omit optional animation metadata. Supply the
// single full-canvas frame, without inventing metadata for animated streams.
inline bool CodecFrameInfo(SkCodec* codec, bool all_received, int index,
                           SkCodec::FrameInfo* info) {
  if (!codec || !info) return false;
  if (codec->getFrameInfo(index, info)) return true;
  if (index != 0 || codec->isAnimated() != SkCodec::IsAnimated::kNo) return false;
  *info = {};
  info->fRequiredFrame = SkCodec::kNoFrame;
  info->fDuration = 0;
  info->fFullyReceived = all_received;
  info->fAlphaType = codec->getInfo().alphaType();
  info->fHasAlphaWithinBounds = info->fAlphaType != kOpaque_SkAlphaType;
  info->fDisposalMethod = SkCodecAnimation::DisposalMethod::kKeep;
  info->fBlend = SkCodecAnimation::Blend::kSrc;
  info->fFrameRect = SkIRect::MakeSize(codec->dimensions());
  return true;
}
}  // namespace charmos
#endif
