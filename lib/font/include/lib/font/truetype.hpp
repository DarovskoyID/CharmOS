#pragma once
#include <stddef.h>
#include <stdint.h>

// Own, allocation-free TrueType reader. Sources: docs/specs/opentype/*.html.
// Static glyf outlines, cmap 4/12, unhinted grayscale. No CFF, shaping or VM.
namespace lib::font {
struct Bytes {
  const uint8_t* data = nullptr;
  size_t size = 0;
  bool has(size_t at, size_t n) const { return at <= size && n <= size - at; }
  uint16_t u16(size_t at) const;
  int16_t i16(size_t at) const { return static_cast<int16_t>(u16(at)); }
  uint32_t u32(size_t at) const;
  Bytes slice(size_t at, size_t n) const;
};
struct Point { double x, y; uint8_t flags; };
struct Edge { double x0, y0, x1, y1; };
struct Crossing { double x; int winding; };
struct Workspace {
  static constexpr uint32_t kPoints = 4096, kContours = 512, kEdges = 16384;
  Point points[kPoints];
  uint32_t ends[kContours];
  Edge edges[kEdges];
  Crossing crossings[kEdges];
  uint32_t point_count, contour_count, edge_count;
};
struct Bitmap {
  uint8_t* pixels;
  uint32_t width, height, stride;
  int32_t left, top;  // top is relative to baseline; y grows downwards
  int32_t advance;
};
class Face {
 public:
  bool open(Bytes file);
  uint16_t glyph(uint32_t codepoint) const;
  int32_t advance(uint16_t id, uint32_t pixels_per_em) const;
  int32_t ascent(uint32_t pixels_per_em) const;
  // Caller supplies output storage and workspace. On failure pixels are not usable.
  bool rasterize(uint16_t id, uint32_t pixels_per_em, Bitmap* out, Workspace* work) const;
  uint16_t glyph_count() const { return glyphs_; }
 private:
  Bytes cmap_, head_, hhea_, hmtx_, loca_, glyf_;
  uint16_t units_ = 0, glyphs_ = 0, metrics_ = 0;
  bool long_loca_ = false;
  bool outline(uint16_t id, Workspace* w, uint32_t depth) const;
  uint32_t location(uint32_t id) const;
};
// Strict Unicode scalar decoding; invalid input consumes one byte and returns U+FFFD.
uint32_t utf8_next(const char** text);
}  // namespace lib::font
