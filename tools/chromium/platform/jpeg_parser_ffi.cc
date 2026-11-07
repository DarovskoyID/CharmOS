// Copyright 2026 The CharmOS Authors
// C++ port of Chromium's Rust JPEG baseline parser.
#include "build/charmos/jpeg_parser_ffi.h"

#include <algorithm>
#include <numeric>

#include "media/parsers/jpeg_parser.h"

namespace media::parsers {

namespace {

constexpr size_t kMaxHuffmanCodeLengths = 16;
constexpr size_t kMaxHuffmanDcSymbols = 12;
constexpr size_t kMaxHuffmanAcSymbols = 162;
constexpr size_t kMaxHuffmanTableNumBaseline = 2;
constexpr size_t kMaxComponents = 4;
constexpr size_t kMaxQuantizationTableNum = 4;
constexpr size_t kMarkerSize = 2;

class Cursor {
 public:
  explicit Cursor(base::span<const uint8_t> data) : data_(data) {}

  bool HasData() const { return position_ < data_.size(); }
  size_t position() const { return position_; }
  void set_position(size_t position) { position_ = position; }

  bool ReadU8(uint8_t* out) {
    if (position_ >= data_.size()) {
      return false;
    }
    *out = data_[position_++];
    return true;
  }

  bool ReadU16(uint16_t* out) {
    if (position_ + 2 > data_.size()) {
      return false;
    }
    *out = static_cast<uint16_t>((data_[position_] << 8) | data_[position_ + 1]);
    position_ += 2;
    return true;
  }

  bool ReadSlice(size_t length, base::span<const uint8_t>* out) {
    if (position_ > data_.size() || length > data_.size() - position_) {
      return false;
    }
    *out = data_.subspan(position_, length);
    position_ += length;
    return true;
  }

  bool Skip(size_t length) {
    if (position_ > data_.size() || length > data_.size() - position_) {
      return false;
    }
    position_ += length;
    return true;
  }

 private:
  base::span<const uint8_t> data_;
  size_t position_ = 0;
};

JpegParserError ParseSof(base::span<const uint8_t> data,
                         JpegFrameHeader* frame_header) {
  Cursor cursor(data);
  frame_header->components.clear();

  uint8_t precision = 0;
  uint16_t visible_height = 0;
  uint16_t visible_width = 0;
  uint8_t num_components = 0;
  if (!cursor.ReadU8(&precision) || !cursor.ReadU16(&visible_height) ||
      !cursor.ReadU16(&visible_width) || !cursor.ReadU8(&num_components)) {
    return JpegParserError::IoError;
  }

  frame_header->visible_height = visible_height;
  frame_header->visible_width = visible_width;
  frame_header->num_components = num_components;

  if (precision != 8) {
    return JpegParserError::Only8BitPrecisionSupported;
  }
  if (num_components < 1 || num_components > kMaxComponents) {
    return JpegParserError::UnsupportedNumberOfComponents;
  }

  uint8_t max_h_factor = 0;
  uint8_t max_v_factor = 0;

  for (uint8_t i = 0; i < num_components; ++i) {
    uint8_t id = 0;
    uint8_t hv = 0;
    uint8_t quantization_table_selector = 0;
    if (!cursor.ReadU8(&id) || !cursor.ReadU8(&hv) ||
        !cursor.ReadU8(&quantization_table_selector)) {
      return JpegParserError::IoError;
    }
    if (id > num_components) {
      return JpegParserError::InvalidComponentId;
    }
    const uint8_t horizontal_sampling_factor = hv >> 4;
    const uint8_t vertical_sampling_factor = hv & 0x0f;
    if (horizontal_sampling_factor > max_h_factor) {
      max_h_factor = horizontal_sampling_factor;
    }
    if (vertical_sampling_factor > max_v_factor) {
      max_v_factor = vertical_sampling_factor;
    }
    if (horizontal_sampling_factor < 1 || horizontal_sampling_factor > 4) {
      return JpegParserError::InvalidHorizontalSamplingFactor;
    }
    if (vertical_sampling_factor < 1 || vertical_sampling_factor > 4) {
      return JpegParserError::InvalidVerticalSamplingFactor;
    }
    if (quantization_table_selector >= kMaxQuantizationTableNum) {
      return JpegParserError::InvalidQuantizationTableSelector;
    }

    frame_header->components.push_back(JpegComponent{
        id, horizontal_sampling_factor, vertical_sampling_factor,
        quantization_table_selector});
  }

  const size_t h_unit = static_cast<size_t>(max_h_factor) * 8;
  const size_t v_unit = static_cast<size_t>(max_v_factor) * 8;
  frame_header->coded_width = static_cast<uint16_t>(
      ((visible_width + h_unit - 1) / h_unit) * h_unit);
  frame_header->coded_height = static_cast<uint16_t>(
      ((visible_height + v_unit - 1) / v_unit) * v_unit);
  return JpegParserError::Ok;
}

JpegParserError ParseDqt(base::span<const uint8_t> data,
                         std::array<JpegQuantizationTable, 4>* q_table) {
  Cursor cursor(data);
  while (cursor.HasData()) {
    uint8_t precision_and_table_id = 0;
    if (!cursor.ReadU8(&precision_and_table_id)) {
      return JpegParserError::IoError;
    }
    const uint8_t precision = precision_and_table_id >> 4;
    const size_t table_id = precision_and_table_id & 0x0f;
    if (precision > 1) {
      return JpegParserError::InvalidQuantizationTablePrecision;
    }
    if (precision == 1) {
      return JpegParserError::Invalid16BitQuantizationTable;
    }
    if (table_id >= kMaxQuantizationTableNum) {
      return JpegParserError::InvalidQuantizationTableId;
    }
    base::span<const uint8_t> q_data;
    if (!cursor.ReadSlice(64, &q_data)) {
      return JpegParserError::UnexpectedEndOfSegment;
    }
    JpegQuantizationTable& table = (*q_table)[table_id];
    table.valid = true;
    base::span(table.value).copy_from(q_data);
  }
  return JpegParserError::Ok;
}

JpegParserError ParseDht(
    base::span<const uint8_t> data,
    std::array<JpegHuffmanTable, 2>* dc_table,
    std::array<JpegHuffmanTable, 2>* ac_table) {
  Cursor cursor(data);
  while (cursor.HasData()) {
    uint8_t table_class_and_id = 0;
    if (!cursor.ReadU8(&table_class_and_id)) {
      return JpegParserError::IoError;
    }
    const uint8_t table_class = table_class_and_id >> 4;
    const size_t table_id = table_class_and_id & 0x0f;
    if (table_class > 1) {
      return JpegParserError::InvalidHuffmanTableClass;
    }
    if (table_id >= kMaxHuffmanTableNumBaseline) {
      return JpegParserError::InvalidHuffmanTableId;
    }

    base::span<const uint8_t> code_length;
    if (!cursor.ReadSlice(kMaxHuffmanCodeLengths, &code_length)) {
      return JpegParserError::UnexpectedEndOfSegment;
    }
    const size_t count = std::accumulate(code_length.begin(),
                                         code_length.end(), size_t{0});
    const size_t max_symbols =
        table_class == 0 ? kMaxHuffmanDcSymbols : kMaxHuffmanAcSymbols;
    if (count > max_symbols) {
      return JpegParserError::TooManyHuffmanCodes;
    }
    base::span<const uint8_t> code_value;
    if (!cursor.ReadSlice(count, &code_value)) {
      return JpegParserError::UnexpectedEndOfSegment;
    }

    JpegHuffmanTable& table =
        table_class == 1 ? (*ac_table)[table_id] : (*dc_table)[table_id];
    table.valid = true;
    base::span(table.code_length).copy_from(code_length);
    base::span(table.code_value).first(count).copy_from(code_value);
  }
  return JpegParserError::Ok;
}

JpegParserError ParseDri(base::span<const uint8_t> data,
                         uint16_t* restart_interval) {
  if (data.size() != 2) {
    return JpegParserError::InvalidDriSegmentSize;
  }
  Cursor cursor(data);
  if (!cursor.ReadU16(restart_interval)) {
    return JpegParserError::IoError;
  }
  return JpegParserError::Ok;
}

JpegParserError ParseSos(base::span<const uint8_t> data,
                         const JpegFrameHeader& frame_header,
                         JpegScanHeader* scan) {
  Cursor cursor(data);
  uint8_t num_components = 0;
  if (!cursor.ReadU8(&num_components)) {
    return JpegParserError::IoError;
  }
  scan->num_components = num_components;
  if (num_components != frame_header.num_components) {
    return JpegParserError::ScanComponentCountMismatch;
  }
  scan->components.clear();
  for (uint8_t i = 0; i < num_components; ++i) {
    uint8_t component_selector = 0;
    uint8_t dc_and_ac_selector = 0;
    if (!cursor.ReadU8(&component_selector) ||
        !cursor.ReadU8(&dc_and_ac_selector)) {
      return JpegParserError::IoError;
    }
    const uint8_t dc_selector = dc_and_ac_selector >> 4;
    const uint8_t ac_selector = dc_and_ac_selector & 0x0f;
    if (component_selector != frame_header.components[i].id) {
      return JpegParserError::ComponentSelectorMismatch;
    }
    if (dc_selector >= kMaxHuffmanTableNumBaseline) {
      return JpegParserError::InvalidDcSelector;
    }
    if (ac_selector >= kMaxHuffmanTableNumBaseline) {
      return JpegParserError::InvalidAcSelector;
    }
    scan->components.push_back(
        JpegScanComponent{component_selector, dc_selector, ac_selector});
  }

  uint8_t spectral_selection_start = 0;
  uint8_t spectral_selection_end = 0;
  uint8_t point_transform = 0;
  if (!cursor.ReadU8(&spectral_selection_start) ||
      !cursor.ReadU8(&spectral_selection_end) ||
      !cursor.ReadU8(&point_transform)) {
    return JpegParserError::IoError;
  }
  if (spectral_selection_start != 0 || spectral_selection_end != 63) {
    return JpegParserError::UnsupportedSpectralSelection;
  }
  if (point_transform != 0) {
    return JpegParserError::UnsupportedPointTransform;
  }
  return JpegParserError::Ok;
}

JpegParserError SearchEoi(base::span<const uint8_t> data,
                          size_t* eoi_begin,
                          size_t* eoi_end) {
  Cursor cursor(data);
  while (cursor.HasData()) {
    size_t pos = cursor.position();
    while (pos < data.size() && data[pos] != JPEG_MARKER_PREFIX) {
      ++pos;
    }
    if (pos >= data.size()) {
      break;
    }
    const size_t marker_pos = pos;
    cursor.set_position(marker_pos + 1);

    uint8_t marker2 = 0;
    do {
      if (!cursor.ReadU8(&marker2)) {
        return JpegParserError::IoError;
      }
    } while (marker2 == JPEG_MARKER_PREFIX);

    if (marker2 == 0x00 || (marker2 >= JPEG_RST0 && marker2 <= JPEG_RST7)) {
      continue;
    }
    if (marker2 == JPEG_EOI) {
      *eoi_begin = marker_pos;
      *eoi_end = cursor.position();
      return JpegParserError::Ok;
    }

    uint16_t size = 0;
    if (!cursor.ReadU16(&size)) {
      return JpegParserError::IoError;
    }
    if (size < 2) {
      return JpegParserError::InvalidMarkerSegmentSize;
    }
    if (!cursor.Skip(size - 2)) {
      return JpegParserError::UnexpectedEndOfImage;
    }
  }
  return JpegParserError::EoiMarkerNotFound;
}

JpegParserError ParseJpegPictureInternal(base::span<const uint8_t> data,
                                         JpegParseResult* result) {
  Cursor cursor(data);
  uint8_t marker1 = 0;
  uint8_t marker2 = 0;
  if (!cursor.ReadU8(&marker1) || !cursor.ReadU8(&marker2)) {
    return JpegParserError::IoError;
  }
  if (marker1 != JPEG_MARKER_PREFIX || marker2 != JPEG_SOI) {
    return JpegParserError::SoiMarkerNotFound;
  }

  bool has_marker_dqt = false;
  bool has_marker_sos = false;
  while (!has_marker_sos) {
    uint8_t m1 = 0;
    if (!cursor.ReadU8(&m1)) {
      return JpegParserError::IoError;
    }
    if (m1 != JPEG_MARKER_PREFIX) {
      return JpegParserError::ExpectedMarkerPrefix;
    }
    uint8_t m2 = 0;
    do {
      if (!cursor.ReadU8(&m2)) {
        return JpegParserError::IoError;
      }
    } while (m2 == JPEG_MARKER_PREFIX);

    uint16_t size = 0;
    if (!cursor.ReadU16(&size)) {
      return JpegParserError::IoError;
    }
    if (size < kMarkerSize) {
      return JpegParserError::InvalidMarkerSegmentSize;
    }
    base::span<const uint8_t> payload;
    if (!cursor.ReadSlice(size - 2, &payload)) {
      return JpegParserError::UnexpectedEndOfSegment;
    }

    switch (m2) {
      case JPEG_SOF0:
        if (JpegParserError error = ParseSof(payload, &result->frame_header);
            error != JpegParserError::Ok) {
          return error;
        }
        break;
      case JPEG_SOF1:
      case JPEG_SOF2:
      case JPEG_SOF3:
      case JPEG_SOF5:
      case JPEG_SOF6:
      case JPEG_SOF7:
      case JPEG_SOF9:
      case JPEG_SOF10:
      case JPEG_SOF11:
      case JPEG_SOF13:
      case JPEG_SOF14:
      case JPEG_SOF15:
        return JpegParserError::OnlySof0Supported;
      case JPEG_DQT:
        if (JpegParserError error = ParseDqt(payload, &result->q_table);
            error != JpegParserError::Ok) {
          return error;
        }
        has_marker_dqt = true;
        break;
      case JPEG_DHT:
        if (JpegParserError error =
                ParseDht(payload, &result->dc_table, &result->ac_table);
            error != JpegParserError::Ok) {
          return error;
        }
        break;
      case JPEG_DRI:
        if (JpegParserError error =
                ParseDri(payload, &result->restart_interval);
            error != JpegParserError::Ok) {
          return error;
        }
        break;
      case JPEG_SOS:
        if (JpegParserError error =
                ParseSos(payload, result->frame_header, &result->scan);
            error != JpegParserError::Ok) {
          return error;
        }
        has_marker_sos = true;
        break;
      default:
        break;
    }
  }

  if (!has_marker_dqt) {
    return JpegParserError::DqtMarkerNotFound;
  }

  const size_t scan_data_start = cursor.position();
  size_t eoi_begin = 0;
  size_t eoi_end = 0;
  if (JpegParserError error =
          SearchEoi(data.subspan(scan_data_start), &eoi_begin, &eoi_end);
      error != JpegParserError::Ok) {
    return error;
  }

  result->data_offset = scan_data_start;
  result->data_length = eoi_begin;
  result->image_size = scan_data_start + eoi_end;
  return JpegParserError::Ok;
}

}  // namespace

JpegParseResult parse_jpeg_picture_ffi(base::span<const uint8_t> data) {
  JpegParseResult result;
  const JpegParserError error = ParseJpegPictureInternal(data, &result);
  if (error != JpegParserError::Ok) {
    JpegParseResult failed;
    failed.error_code = error;
    return failed;
  }
  result.error_code = JpegParserError::Ok;
  return result;
}

}  // namespace media::parsers
