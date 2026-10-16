#include <lib/font/truetype.hpp>
#include <fstream>
#include <iterator>
#include <vector>
#include "test.hpp"

using namespace lib::font;
namespace {
Workspace work;
std::vector<uint8_t> load(const char* name) {
  std::ifstream f(std::string(CHARMOS_FONT_DATA) + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
bool draw(const Face& face, uint32_t cp, std::vector<uint8_t>* image, Bitmap* bitmap) {
  *bitmap = {image->data(), 128, 128, 128, 0, 0, 0};
  return face.rasterize(face.glyph(cp), 32, bitmap, &work);
}
}

TEST(font_unicode_and_metrics) {
  auto data = load("jetbrains-mono/JetBrainsMono-Regular.ttf");
  Face f;
  CHECK(f.open({data.data(), data.size()}));
  CHECK(f.glyph('A') != 0 && f.glyph(0x416) != 0 && f.glyph(0x1f600) == 0);
  CHECK(f.glyph(0x110000) == 0 && f.glyph(0xd800) == 0);
  CHECK(f.advance(f.glyph('i'), 20) == f.advance(f.glyph('W'), 20));
  CHECK(f.advance(f.glyph('W'), 20) > 0 && f.ascent(20) > 0);
  const char* s = "A\xc3\xa9\xd0\x96\xf0\x9f\x98\x80";
  CHECK(utf8_next(&s) == 'A' && utf8_next(&s) == 0xe9 && utf8_next(&s) == 0x416 && utf8_next(&s) == 0x1f600 && utf8_next(&s) == 0);
  for (const char* bad : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2", "\x80"}) {
    const char* p = bad;
    CHECK(utf8_next(&p) == 0xfffd && p == bad + 1);
  }
}

TEST(font_coverage_and_composites) {
  for (const char* name : {"space-grotesk/SpaceGrotesk-Regular.ttf", "space-grotesk/SpaceGrotesk-Medium.ttf", "jetbrains-mono/JetBrainsMono-Regular.ttf"}) {
    auto data = load(name); Face f;
    CHECK(f.open({data.data(), data.size()}));
    std::vector<uint8_t> pixels(128 * 128); Bitmap b{};
    CHECK(draw(f, ' ', &pixels, &b) && b.width == 0 && b.advance > 0);
    for (uint32_t cp : {uint32_t{'A'}, uint32_t{'O'}, uint32_t{0xe9}}) {
      CHECK(f.glyph(cp) != 0 && draw(f, cp, &pixels, &b));
      unsigned full = 0, partial = 0;
      for (uint32_t y = 0; y < b.height; ++y) for (uint32_t x = 0; x < b.width; ++x) {
        uint8_t a = pixels[y * b.stride + x];
        full += a == 255; partial += a > 0 && a < 255;
      }
      CHECK(full > 5 && partial > 5);
      if (cp == 'O') CHECK(pixels[(b.height / 2) * b.stride + b.width / 2] == 0);
    }
    // Exercise every shipped glyph, including composite accents and Cyrillic.
    for (uint32_t id = 0; id < f.glyph_count(); ++id) {
      b = {pixels.data(), 128, 128, 128, 0, 0, 0};
      if (!f.rasterize(static_cast<uint16_t>(id), 32, &b, &work)) {
        std::printf("glyph rejected: %s %u\n", name, id);
        CHECK(false); break;
      }
    }
  }
}

TEST(font_malformed_and_limits) {
  auto original = load("space-grotesk/SpaceGrotesk-Regular.ttf");
  Face f;
  CHECK(!f.open({nullptr, 100}));
  for (size_t n = 0; n < original.size(); n += 131) CHECK(!f.open({original.data(), n}));
  auto data = original; data[12 + 8] = 0xff;
  CHECK(!f.open({data.data(), data.size()}));
  CHECK(f.open({original.data(), original.size()}));
  uint8_t small[1]{}; Bitmap b{small, 1, 1, 1, 0, 0, 0};
  CHECK(!f.rasterize(f.glyph('W'), 32, &b, &work));
  CHECK(!f.rasterize(0, 0, &b, &work));
  // Deterministic mutation smoke test, also run under ASan/UBSan.
  uint32_t rng = 41;
  std::vector<uint8_t> pixels(128 * 128);
  for (unsigned i = 0; i < 300; ++i) {
    data = original;
    rng = rng * 1664525 + 1013904223;
    size_t at = rng % data.size(); data[at] ^= static_cast<uint8_t>(rng >> 24);
    if (!f.open({data.data(), data.size()})) continue;
    b = {pixels.data(), 128, 128, 128, 0, 0, 0};
    (void)f.rasterize(f.glyph(static_cast<uint32_t>('A') + i % 64), 32, &b, &work);
  }
}

TEST(font_preview) {
  auto data = load("space-grotesk/SpaceGrotesk-Regular.ttf"); Face f;
  CHECK(f.open({data.data(), data.size()}));
  constexpr unsigned width = 720, height = 140;
  std::vector<uint8_t> rgb(width * height * 3, 12), pixels(128 * 128);
  const char* s = "CharmOS  Space Grotesk  Aa 0123 e\xc3\xa9";
  int32_t x = 16;
  while (*s != 0) {
    Bitmap b{}; CHECK(draw(f, utf8_next(&s), &pixels, &b));
    for (uint32_t y = 0; y < b.height; ++y) for (uint32_t xx = 0; xx < b.width; ++xx) {
      int32_t dx = x + b.left + static_cast<int32_t>(xx), dy = 65 + b.top + static_cast<int32_t>(y);
      if (dx < 0 || dy < 0 || dx >= static_cast<int32_t>(width) || dy >= static_cast<int32_t>(height)) continue;
      size_t at = (static_cast<size_t>(dy) * width + static_cast<uint32_t>(dx)) * 3;
      uint32_t a = pixels[y * b.stride + xx];
      rgb[at] = static_cast<uint8_t>((245 * a + 12 * (255 - a)) / 255);
      rgb[at + 1] = static_cast<uint8_t>((197 * a + 12 * (255 - a)) / 255);
      rgb[at + 2] = static_cast<uint8_t>((24 * a + 12 * (255 - a)) / 255);
    }
    x += b.advance;
  }
  std::ofstream out("font-preview.ppm", std::ios::binary);
  out << "P6\n" << width << ' ' << height << "\n255\n";
  out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
}
