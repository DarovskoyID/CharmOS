// Copyright 2026 The CharmOS Authors
// C++ replacement for Chromium's Rust JPEG picture parser (`parse_jpeg.rs`).
// The structs mirror the former cxx bridge so media/parsers/jpeg_parser.cc can
// keep its field-by-field conversion code.

#ifndef BUILD_CHARMOS_JPEG_PARSER_FFI_H_
#define BUILD_CHARMOS_JPEG_PARSER_FFI_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <vector>

#include "base/containers/span.h"

namespace media::parsers {

enum class JpegParserError {
  Ok = 0,
  IoError = 1,
  UnexpectedEndOfSegment = 2,
  Only8BitPrecisionSupported = 3,
  UnsupportedNumberOfComponents = 4,
  InvalidComponentId = 5,
  InvalidHorizontalSamplingFactor = 6,
  InvalidVerticalSamplingFactor = 7,
  InvalidQuantizationTablePrecision = 8,
  Invalid16BitQuantizationTable = 9,
  InvalidQuantizationTableId = 10,
  InvalidHuffmanTableClass = 11,
  InvalidHuffmanTableId = 12,
  TooManyHuffmanCodes = 13,
  InvalidDriSegmentSize = 14,
  ScanComponentCountMismatch = 15,
  ComponentSelectorMismatch = 16,
  InvalidDcSelector = 17,
  InvalidAcSelector = 18,
  UnsupportedSpectralSelection = 19,
  UnsupportedPointTransform = 20,
  InvalidMarkerSegmentSize = 21,
  UnexpectedEndOfImage = 22,
  EoiMarkerNotFound = 23,
  SoiMarkerNotFound = 24,
  ExpectedMarkerPrefix = 25,
  OnlySof0Supported = 26,
  DqtMarkerNotFound = 27,
  InvalidQuantizationTableSelector = 28,
};

struct JpegComponent {
  uint8_t id = 0;
  uint8_t horizontal_sampling_factor = 0;
  uint8_t vertical_sampling_factor = 0;
  uint8_t quantization_table_selector = 0;
};

struct JpegFrameHeader {
  uint16_t visible_width = 0;
  uint16_t visible_height = 0;
  uint16_t coded_width = 0;
  uint16_t coded_height = 0;
  uint8_t num_components = 0;
  std::vector<JpegComponent> components;
};

struct JpegScanComponent {
  uint8_t component_selector = 0;
  uint8_t dc_selector = 0;
  uint8_t ac_selector = 0;
};

struct JpegScanHeader {
  uint8_t num_components = 0;
  std::vector<JpegScanComponent> components;
};

struct JpegHuffmanTable {
  bool valid = false;
  std::array<uint8_t, 16> code_length{};
  std::array<uint8_t, 162> code_value{};
};

struct JpegQuantizationTable {
  bool valid = false;
  std::array<uint8_t, 64> value{};
};

struct JpegParseResult {
  JpegParserError error_code = JpegParserError::Ok;
  JpegFrameHeader frame_header;
  std::array<JpegHuffmanTable, 2> dc_table;
  std::array<JpegHuffmanTable, 2> ac_table;
  std::array<JpegQuantizationTable, 4> q_table;
  uint16_t restart_interval = 0;
  JpegScanHeader scan;
  size_t data_offset = 0;
  size_t data_length = 0;
  size_t image_size = 0;
};

JpegParseResult parse_jpeg_picture_ffi(base::span<const uint8_t> data);

}  // namespace media::parsers

#endif  // BUILD_CHARMOS_JPEG_PARSER_FFI_H_
