// Copyright 2026 The CharmOS Authors
// C++ port of Chromium's Rust MPEG/ADTS audio header parsers.
#include "build/charmos/mpeg_audio_ffi.h"

#include <algorithm>
#include <cstring>

namespace media::formats::mpeg {

namespace {

enum class ParseError { kOk, kNeedMoreData, kInvalidHeader };

constexpr uint32_t kIcyStartCode = 0x49435920;  // "ICY "
constexpr uint32_t kId3StartCodeMask = 0xffffff00;
constexpr uint32_t kId3v1StartCode = 0x54414700;  // "TAG\0"
constexpr uint32_t kId3v2StartCode = 0x49443300;  // "ID3\0"

uint32_t ReadBe32(base::span<const uint8_t> data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

// ---------------------------------------------------------------------------
// MP3
// ---------------------------------------------------------------------------

constexpr size_t kMp3HeaderSize = 4;
constexpr int kVersion2 = 2;
constexpr int kVersionReserved = 1;
constexpr int kVersion2_5 = 0;
constexpr int kLayer1 = 3;
constexpr int kLayer2 = 2;
constexpr int kLayer3 = 1;
constexpr int kLayerReserved = 0;

constexpr uint32_t kBitrateMap[16][6] = {
    {0, 0, 0, 0, 0, 0},       {32, 32, 32, 32, 8, 0},
    {64, 48, 40, 48, 16, 0},  {96, 56, 48, 56, 24, 0},
    {128, 64, 56, 64, 32, 0}, {160, 80, 64, 80, 40, 0},
    {192, 96, 80, 96, 48, 0}, {224, 112, 96, 112, 56, 0},
    {256, 128, 112, 128, 64, 0}, {288, 160, 128, 144, 80, 0},
    {320, 192, 160, 160, 96, 0}, {352, 224, 192, 176, 112, 0},
    {384, 256, 224, 192, 128, 0}, {416, 320, 256, 224, 144, 0},
    {448, 384, 320, 256, 160, 0}, {0, 0, 0, 0, 0, 0},
};

constexpr uint32_t kSampleRateMap[4][4] = {
    {11025, 0, 22050, 44100},
    {12000, 0, 24000, 48000},
    {8000, 0, 16000, 32000},
    {0, 0, 0, 0},
};

// {MPEG 2.5, reserved, MPEG 2, MPEG 1} x {reserved, L3, L2, L1}
constexpr int kVersionLayerMap[4][4] = {
    {5, 4, 4, 3},
    {5, 5, 5, 5},
    {5, 4, 4, 3},
    {5, 2, 1, 0},
};

constexpr int kXingHeaderMap[2][2] = {{32, 17}, {17, 9}};

ParseError ParseMp3HeaderInternal(base::span<const uint8_t> data,
                                  MpegAudioHeaderInfo* info) {
  if (data.size() < kMp3HeaderSize) {
    return ParseError::kNeedMoreData;
  }
  const uint32_t w = ReadBe32(data);
  const uint32_t sync = w >> 21;
  const int version = (w >> 19) & 0x3;
  const int layer = (w >> 17) & 0x3;
  const uint32_t bitrate_index = (w >> 12) & 0xf;
  const uint32_t sample_rate_index = (w >> 10) & 0x3;
  const bool has_padding = ((w >> 9) & 1) != 0;
  const int channel_mode = (w >> 6) & 0x3;

  if (sync != 0x7ff || version == kVersionReserved ||
      layer == kLayerReserved || bitrate_index == 0 ||
      bitrate_index == 0xf || sample_rate_index == 3) {
    return ParseError::kInvalidHeader;
  }

  const uint32_t bitrate = kBitrateMap[bitrate_index][kVersionLayerMap[version][layer]];
  if (bitrate == 0) {
    return ParseError::kInvalidHeader;
  }
  const uint32_t frame_sample_rate = kSampleRateMap[sample_rate_index][version];
  if (frame_sample_rate == 0) {
    return ParseError::kInvalidHeader;
  }

  uint32_t samples_per_frame = 0;
  switch (layer) {
    case kLayer1:
      samples_per_frame = 384;
      break;
    case kLayer2:
      samples_per_frame = 1152;
      break;
    case kLayer3:
      samples_per_frame =
          (version == kVersion2 || version == kVersion2_5) ? 576 : 1152;
      break;
    default:
      return ParseError::kInvalidHeader;
  }

  uint64_t frame_size = 0;
  if (layer == kLayer1) {
    frame_size = 4ull * (12ull * bitrate * 1000 / frame_sample_rate);
  } else {
    frame_size = (static_cast<uint64_t>(samples_per_frame) / 8) * bitrate *
                 1000 / frame_sample_rate;
  }
  if (has_padding) {
    frame_size += (layer == kLayer1) ? 4 : 1;
  }

  bool metadata_frame = false;
  if (layer == kLayer3) {
    const int xing_header_index =
        kXingHeaderMap[(version == kVersion2 || version == kVersion2_5) ? 1 : 0]
                      [channel_mode == 3 ? 1 : 0];
    const size_t tag_size = 4;
    const size_t needed = kMp3HeaderSize + xing_header_index + tag_size;
    if (frame_size >= needed && data.size() >= needed) {
      const uint8_t* tag = data.data() + kMp3HeaderSize + xing_header_index;
      if (std::memcmp(tag, "Info", 4) == 0 ||
          std::memcmp(tag, "Xing", 4) == 0) {
        metadata_frame = true;
      }
    }
  }

  info->frame_size = static_cast<size_t>(frame_size);
  info->sample_rate = frame_sample_rate;
  info->channels = (channel_mode == 3) ? 1 : 2;
  info->sample_count = samples_per_frame;
  info->metadata_frame = metadata_frame;
  info->esds = 0;
  return ParseError::kOk;
}

// ---------------------------------------------------------------------------
// ADTS
// ---------------------------------------------------------------------------

constexpr size_t kAdtsHeaderSize = 7;
constexpr uint32_t kSamplesPerAacFrame = 1024;

constexpr uint32_t kAdtsFrequencyTable[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000,
    22050, 16000, 12000, 11025, 8000,  7350,
};

constexpr uint32_t kAdtsChannelsTable[] = {0, 1, 2, 3, 4, 5, 6, 8};

ParseError ParseAdtsHeaderInternal(base::span<const uint8_t> data,
                                   MpegAudioHeaderInfo* info) {
  if (data.size() < kAdtsHeaderSize) {
    return ParseError::kNeedMoreData;
  }

  uint64_t w = 0;
  for (size_t i = 0; i < kAdtsHeaderSize; ++i) {
    w = (w << 8) | data[i];
  }

  const uint32_t sync = (w >> 52) & 0xfff;
  const uint32_t layer = (w >> 49) & 0x3;
  const uint32_t protection_absent = (w >> 48) & 0x1;
  const uint32_t profile = (w >> 46) & 0x3;
  const uint32_t sample_rate_index = (w >> 42) & 0xf;
  const uint32_t channel_layout_index = (w >> 38) & 0x7;
  const uint32_t frame_length = (w >> 21) & 0x1fff;
  const uint32_t num_data_blocks = (w >> 8) & 0x3;

  size_t bytes_read = kAdtsHeaderSize;
  if (protection_absent == 0) {
    if (data.size() < kAdtsHeaderSize + 2) {
      return ParseError::kNeedMoreData;
    }
    bytes_read += 2;
  }

  if (sync != 0xfff || layer != 0 || frame_length < bytes_read ||
      sample_rate_index >= std::size(kAdtsFrequencyTable) ||
      channel_layout_index >= std::size(kAdtsChannelsTable)) {
    return ParseError::kInvalidHeader;
  }

  const uint16_t esds = static_cast<uint16_t>(
      (((((profile + 1) << 4) + sample_rate_index) << 4) +
       channel_layout_index)
      << 3);

  info->frame_size = frame_length;
  info->sample_rate = kAdtsFrequencyTable[sample_rate_index];
  info->channels = kAdtsChannelsTable[channel_layout_index];
  info->sample_count = (num_data_blocks + 1) * kSamplesPerAacFrame;
  info->metadata_frame = false;
  info->esds = esds;
  return ParseError::kOk;
}

// ---------------------------------------------------------------------------
// Shared action parser
// ---------------------------------------------------------------------------

using ParseFn = ParseError (*)(base::span<const uint8_t>, MpegAudioHeaderInfo*);

constexpr uint32_t kAdtsStartCode = 0xfff00000;
constexpr uint32_t kMp3StartCode = 0xffe00000;

MpegAudioParserAction ParseFrameAction(base::span<const uint8_t> data,
                                       ParseFn parse_fn) {
  MpegAudioParserAction result;
  MpegAudioHeaderInfo info;
  const ParseError error = parse_fn(data, &info);
  if (error == ParseError::kOk) {
    if (data.size() < info.frame_size) {
      result.action_type = ActionType::NeedMoreData;
      result.partial_frame = true;
      return result;
    }
    result.action_type = ActionType::AudioFrame;
    result.header_info = info;
    return result;
  }
  if (error == ParseError::kNeedMoreData) {
    result.action_type = ActionType::NeedMoreData;
    result.partial_frame = true;
  } else {
    result.action_type = ActionType::Error;
  }
  return result;
}

// Returns the offset just past a blank-line-terminated header block, or -1.
int LocateEndOfHeaders(base::span<const uint8_t> buf) {
  bool was_lf = false;
  uint8_t last_b = 0;
  for (size_t i = 0; i < buf.size(); ++i) {
    const uint8_t b = buf[i];
    if (b == '\n') {
      if (was_lf) {
        return static_cast<int>(i + 1);
      }
      was_lf = true;
    } else if (b != '\r' || last_b != '\n') {
      was_lf = false;
    }
    last_b = b;
  }
  return -1;
}

MpegAudioParserAction ParseIcecastAction(base::span<const uint8_t> data) {
  constexpr size_t kMaxIcecastHeaderSize = 4096;
  constexpr size_t kIcyStartCodeLen = 4;
  MpegAudioParserAction result;
  const size_t locate_size = std::min(data.size(), kMaxIcecastHeaderSize);
  if (locate_size <= kIcyStartCodeLen) {
    result.action_type = ActionType::NeedMoreData;
    return result;
  }
  const int offset = LocateEndOfHeaders(data.subspan(
      kIcyStartCodeLen, locate_size - kIcyStartCodeLen));
  if (offset >= 0) {
    result.action_type = ActionType::Metadata;
    result.bytes_to_skip = kIcyStartCodeLen + static_cast<size_t>(offset);
  } else if (locate_size == kMaxIcecastHeaderSize) {
    result.action_type = ActionType::Error;
  } else {
    result.action_type = ActionType::NeedMoreData;
  }
  return result;
}

MpegAudioParserAction ParseId3v1Action(base::span<const uint8_t> data) {
  constexpr size_t kId3v1Size = 128;
  constexpr size_t kId3v1ExtendedSize = 227;
  MpegAudioParserAction result;
  const bool extended = data.size() >= 4 && data[0] == 'T' && data[1] == 'A' &&
                        data[2] == 'G' && data[3] == '+';
  const size_t needed = extended ? kId3v1ExtendedSize : kId3v1Size;
  if (data.size() < needed) {
    result.action_type = ActionType::NeedMoreData;
  } else {
    result.action_type = ActionType::Metadata;
    result.bytes_to_skip = needed;
  }
  return result;
}

bool ParseSyncSafeInt(base::span<const uint8_t> bytes, size_t* value) {
  size_t result = 0;
  for (uint8_t b : bytes) {
    if ((b & 0x80) != 0) {
      return false;
    }
    result = (result << 7) | b;
  }
  *value = result;
  return true;
}

MpegAudioParserAction ParseId3v2Action(base::span<const uint8_t> data) {
  MpegAudioParserAction result;
  if (data.size() < 10) {
    result.action_type = ActionType::NeedMoreData;
    return result;
  }
  const uint8_t flags = data[5];
  size_t id3_size = 0;
  if (!ParseSyncSafeInt(data.subspan(size_t{6}, size_t{4}), &id3_size)) {
    result.action_type = ActionType::Error;
    return result;
  }
  size_t actual_tag_size = 10 + id3_size;
  if ((flags & 0x10) != 0) {
    actual_tag_size += 10;
  }
  if (data.size() < actual_tag_size) {
    result.action_type = ActionType::NeedMoreData;
  } else {
    result.action_type = ActionType::Metadata;
    result.bytes_to_skip = actual_tag_size;
  }
  return result;
}

MpegAudioParserAction FindNextStartCodeAction(base::span<const uint8_t> data,
                                              ParseFn parse_fn) {
  MpegAudioParserAction result;
  size_t start = 0;
  while (start < data.size()) {
    size_t pos = start;
    while (pos < data.size() && data[pos] != 0xff) {
      ++pos;
    }
    if (pos >= data.size()) {
      break;
    }
    const size_t candidate = pos;
    size_t current_sync = candidate;
    bool parse_failed = false;

    for (int i = 0; i < 3; ++i) {
      MpegAudioHeaderInfo info;
      const ParseError error = parse_fn(data.subspan(current_sync), &info);
      if (error == ParseError::kOk) {
        if (info.frame_size == 0) {
          parse_failed = true;
          break;
        }
        current_sync += info.frame_size;
        if (current_sync >= data.size()) {
          result.action_type = ActionType::NeedMoreData;
          return result;
        }
      } else if (error == ParseError::kNeedMoreData) {
        result.action_type = ActionType::NeedMoreData;
        return result;
      } else {
        parse_failed = true;
        break;
      }
    }

    if (!parse_failed) {
      result.action_type = ActionType::Skip;
      result.bytes_to_skip = candidate;
      return result;
    }
    start = candidate + 1;
  }
  result.action_type = ActionType::NeedMoreData;
  return result;
}

MpegAudioParserAction ParseOneAction(base::span<const uint8_t> data,
                                     uint32_t start_code,
                                     ParseFn parse_fn) {
  MpegAudioParserAction result;
  if (data.size() < 4) {
    result.action_type = ActionType::NeedMoreData;
    return result;
  }
  const uint32_t raw_start_code = ReadBe32(data);
  if ((raw_start_code & start_code) == start_code) {
    return ParseFrameAction(data, parse_fn);
  }
  if (raw_start_code == kIcyStartCode) {
    return ParseIcecastAction(data);
  }
  if ((raw_start_code & kId3StartCodeMask) == kId3v1StartCode) {
    return ParseId3v1Action(data);
  }
  if ((raw_start_code & kId3StartCodeMask) == kId3v2StartCode) {
    return ParseId3v2Action(data);
  }
  return FindNextStartCodeAction(data, parse_fn);
}

}  // namespace

MpegAudioHeaderInfo parse_mp3_header(base::span<const uint8_t> data) {
  MpegAudioHeaderInfo info;
  if (ParseMp3HeaderInternal(data, &info) != ParseError::kOk) {
    return MpegAudioHeaderInfo();
  }
  return info;
}

MpegAudioHeaderInfo parse_adts_header(base::span<const uint8_t> data) {
  MpegAudioHeaderInfo info;
  if (ParseAdtsHeaderInternal(data, &info) != ParseError::kOk) {
    return MpegAudioHeaderInfo();
  }
  return info;
}

MpegAudioParserAction parse_mp3_action(base::span<const uint8_t> data) {
  return ParseOneAction(data, kMp3StartCode, &ParseMp3HeaderInternal);
}

MpegAudioParserAction parse_adts_action(base::span<const uint8_t> data) {
  return ParseOneAction(data, kAdtsStartCode, &ParseAdtsHeaderInternal);
}

}  // namespace media::formats::mpeg
