// Copyright 2026 The CharmOS Authors
// C++ replacement for the Rust MPEG/ADTS audio header parsers that Chromium 155
// migrated to `third_party/rust`. CharmOS production code is C++ only.
//
// The types and function names mirror the former Rust cxx bridge so the media
// stack can call them unchanged.

#ifndef BUILD_CHARMOS_MPEG_AUDIO_FFI_H_
#define BUILD_CHARMOS_MPEG_AUDIO_FFI_H_

#include <stddef.h>
#include <stdint.h>

#include "base/containers/span.h"

namespace media::formats::mpeg {

enum class ActionType {
  NeedMoreData = 1,
  Error = 2,
  Skip = 3,
  Metadata = 4,
  AudioFrame = 5,
};

// Combined structure for MP3 and AAC. Fields that do not apply stay zero.
struct MpegAudioHeaderInfo {
  size_t frame_size = 0;
  uint32_t sample_rate = 0;
  uint32_t channels = 0;
  uint32_t sample_count = 0;

  // MP3 only.
  bool metadata_frame = false;

  // AAC only.
  uint16_t esds = 0;
};

struct MpegAudioParserAction {
  ActionType action_type = ActionType::NeedMoreData;
  size_t bytes_to_skip = 0;
  MpegAudioHeaderInfo header_info;
  bool partial_frame = false;
};

MpegAudioHeaderInfo parse_mp3_header(base::span<const uint8_t> data);
MpegAudioHeaderInfo parse_adts_header(base::span<const uint8_t> data);
MpegAudioParserAction parse_mp3_action(base::span<const uint8_t> data);
MpegAudioParserAction parse_adts_action(base::span<const uint8_t> data);

}  // namespace media::formats::mpeg

#endif  // BUILD_CHARMOS_MPEG_AUDIO_FFI_H_
