#pragma once
#include <lib/font/truetype.hpp>
#include <stdint.h>

// Small, bounded software canvas for native window clients. Font parsing and
// rasterization are shared with wm through lib/font; no syscall/UI ABI changes.
namespace charm::gui {
class Canvas {
 public:
  bool fonts(lib::font::Bytes ui, lib::font::Bytes mono) { return ui_.open(ui) && mono_.open(mono); }
  void target(uint32_t* pixels, int width, int height, bool bgrx) { pixels_ = pixels; width_ = width; height_ = height; bgrx_ = bgrx; }
  void fill(int x, int y, int w, int h, uint32_t color) {
    int right = x + w < width_ ? x + w : width_, bottom = y + h < height_ ? y + h : height_;
    if (x < 0) x = 0; if (y < 0) y = 0;
    color = pixel(color);
    for (int py = y; py < bottom; ++py) for (int px = x; px < right; ++px) pixels_[py * width_ + px] = color;
  }
  void rounded(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    for (int row = 0; row < h; ++row) {
      int dy = row < radius ? radius - row : (row >= h - radius ? row - (h - radius - 1) : 0);
      int inset = 0;
      while (inset < radius && (radius - inset) * (radius - inset) + dy * dy > radius * radius) ++inset;
      fill(x + inset, y + row, w - inset * 2, 1, color);
    }
  }
  void text(const char* value, int x, int baseline, uint32_t color, int right, unsigned size = 14, bool mono = false) {
    if (right > width_) right = width_;
    uint32_t fg = pixel(color);
    while (*value != 0 && x < right) {
      uint32_t cp = lib::font::utf8_next(&value);
      Glyph* cached = nullptr;
      for (auto& g : glyphs_) if (g.used && g.cp == cp && g.size == size && g.mono == mono) { cached = &g; break; }
      if (cached == nullptr) {
        cached = &glyphs_[next_++ % 128]; *cached = {};
        cached->cp = cp; cached->size = size; cached->mono = mono;
        cached->bitmap = {cached->alpha, 48, 48, 48, 0, 0, 0};
        auto& face = mono || ui_.glyph(cp) == 0 ? mono_ : ui_;
        if (!face.rasterize(face.glyph(cp), size, &cached->bitmap, &work_)) continue;
        cached->used = true;
      }
      auto& b = cached->bitmap;
      for (uint32_t y = 0; y < b.height; ++y) for (uint32_t px = 0; px < b.width; ++px) {
        int tx = x + b.left + static_cast<int>(px), ty = baseline + b.top + static_cast<int>(y);
        if (tx < 0 || tx >= right || ty < 0 || ty >= height_) continue;
        uint32_t a = cached->alpha[y * b.stride + px], out = 0;
        auto& dst = pixels_[ty * width_ + tx];
        for (unsigned shift = 0; shift < 24; shift += 8)
          out |= ((((fg >> shift) & 255) * a + ((dst >> shift) & 255) * (255 - a) + 127) / 255) << shift;
        dst = out;
      }
      x += b.advance;
    }
  }
 private:
  uint32_t pixel(uint32_t c) const { return bgrx_ ? c : ((c & 255) << 16) | (c & 0xff00) | (c >> 16); }
  struct Glyph {
    bool used, mono; uint32_t cp, size; lib::font::Bitmap bitmap; uint8_t alpha[48 * 48];
  } glyphs_[128]{};
  uint32_t* pixels_ = nullptr;
  int width_ = 0, height_ = 0;
  bool bgrx_ = true;
  unsigned next_ = 0;
  lib::font::Face ui_, mono_;
  lib::font::Workspace work_;
};
}  // namespace charm::gui
