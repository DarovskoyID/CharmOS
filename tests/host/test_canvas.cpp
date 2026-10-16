#include <charm/canvas.hpp>
#include "test.hpp"
#include <fstream>
#include <vector>

namespace {
std::vector<uint8_t> read_font(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  auto size = file.tellg(); file.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(data.data()), size); return data;
}
charm::gui::Canvas canvas;
}
TEST(canvas_clips_pixels_text_and_glyph_cache_eviction) {
  auto ui = read_font(CHARMOS_FONT_DATA "space-grotesk/SpaceGrotesk-Medium.ttf");
  auto mono = read_font(CHARMOS_FONT_DATA "jetbrains-mono/JetBrainsMono-Regular.ttf");
  CHECK(!ui.empty() && !mono.empty());
  CHECK(canvas.fonts({ui.data(), ui.size()}, {mono.data(), mono.size()}));
  uint32_t buffer[80]; for (auto& value : buffer) value = 0xdeadbeef;
  canvas.target(buffer + 8, 8, 8, true);
  canvas.fill(-3, -4, 20, 20, 0xf5c518);
  for (int i = 8; i < 72; ++i) CHECK(buffer[i] == 0xf5c518);
  canvas.fill(0, 0, 8, 8, 0);
  for (unsigned size = 10; size < 23; ++size) for (char letter = 'A'; letter <= 'Z'; ++letter) {
    char text[] = {letter, 0}; canvas.text(text, -4, 6, 0xffffff, 4, size);
  }
  canvas.text("Файлы\xff", -10, -2, 0xffffff, 4, 16);
  for (int y = 0; y < 8; ++y) for (int x = 4; x < 8; ++x) CHECK(buffer[8 + y * 8 + x] == 0);
  canvas.rounded(-4, -4, 16, 16, 6, 0x121214);
  for (int i = 0; i < 8; ++i) CHECK(buffer[i] == 0xdeadbeef && buffer[i + 72] == 0xdeadbeef);
  canvas.target(buffer + 8, 8, 8, false); canvas.fill(0, 0, 1, 1, 0x112233);
  CHECK(buffer[8] == 0x332211);
}
