#include <charm/canvas.hpp>
#include <charm/window.hpp>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
namespace sc = abi::syscall;
namespace wm = abi::wm;
constexpr int kWidth = 860, kHeight = 480;
extern "C" const uint8_t wm_ui_font[], wm_ui_font_end[], wm_mono_font[], wm_mono_font_end[];
charm::gui::Canvas canvas;
struct Entry { char name[256]; bool directory; long long size; } entries[128]{};
char path[512] = "/", query[64]{}, preview[4097]{}, notice[128]{};
int count = 0, selected = 0, first = 0, query_len = 0;
bool viewing = false, search = false, truncated = false;
unsigned shift_keys = 0;
int pointer_x = 0, pointer_y = 0;
uint16_t preview_lines[512]{};
int preview_count = 0, preview_first = 0;
int hover = -1;

int hit(int x, int y) {
  if (y >= 10 && y < 46) {
    if (x >= 10 && x < 88) return 1000;
    if (x >= 92 && x < 206) return 1001;
  }
  if (x >= 10 && x < 164 && y >= 102 && y < 216 && (y - 102) % 38 < 32) return 1010 + (y - 102) / 38;
  if (!viewing && x >= 186 && x < 846 && y >= 100 && y < 420) return (y - 100) / 32;
  return -1;
}
void layout_preview() {
  preview_count = 0; preview_first = 0;
  const char* p = preview;
  while (*p && preview_count < 512) {
    preview_lines[preview_count++] = static_cast<uint16_t>(p - preview);
    int columns = 0;
    while (*p && *p != '\n' && columns < 78) { lib::font::utf8_next(&p); ++columns; }
    if (*p == '\n') ++p;
  }
}
void scroll_preview(int step) {
  preview_first += step;
  int maximum = preview_count > 16 ? preview_count - 16 : 0;
  if (preview_first < 0) preview_first = 0;
  if (preview_first > maximum) preview_first = maximum;
}

bool matches(int index) {
  if (query_len == 0) return true;
  auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c; };
  for (const char* start = entries[index].name; *start != 0; ++start) {
    const char* a = start; const char* b = query;
    while (*a != 0 && *b != 0 && lower(static_cast<unsigned char>(*a)) == lower(static_cast<unsigned char>(*b))) { ++a; ++b; }
    if (*b == 0) return true;
  }
  return false;
}
int matching_count() { int n = 0; for (int i = 0; i < count; ++i) if (matches(i)) ++n; return n; }
int index_at(int row) {
  if (row < 0) return -1;
  for (int i = 0; i < count; ++i) if (matches(i) && row-- == 0) return i;
  return -1;
}
void fullpath(const char* name, char* out, size_t cap) { snprintf(out, cap, "%s%s%s", path, strcmp(path, "/") == 0 ? "" : "/", name); }
void reload() {
  count = 0; selected = 0; first = 0; viewing = false; truncated = false; notice[0] = 0;
  DIR* directory = opendir(path);
  if (directory == nullptr) { snprintf(notice, sizeof notice, "Не удалось открыть каталог (ошибка %d)", errno); return; }
  errno = 0;
  while (auto* item = readdir(directory)) {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
    if (count == 128) { truncated = true; break; }
    auto& e = entries[count++]; snprintf(e.name, sizeof e.name, "%s", item->d_name);
    char absolute[1024]; fullpath(e.name, absolute, sizeof absolute);
    struct stat st{};
    if (stat(absolute, &st) == 0) { e.directory = S_ISDIR(st.st_mode); e.size = st.st_size; }
    else { e.directory = item->d_type == DT_DIR; e.size = -1; }
  }
  closedir(directory);
  for (int i = 1; i < count; ++i) for (int j = i; j > 0; --j) {
    const auto& a = entries[j - 1]; const auto& b = entries[j];
    if ((a.directory && !b.directory) || (a.directory == b.directory && strcmp(a.name, b.name) <= 0)) break;
    Entry swap = entries[j - 1]; entries[j - 1] = entries[j]; entries[j] = swap;
  }
  selected = index_at(0);
  printf("files: directory %s (%d entries)\n", path, count);
}
void parent() {
  if (viewing) { viewing = false; return; }
  char* slash = strrchr(path, '/');
  if (slash == path) path[1] = 0; else if (slash != nullptr) *slash = 0;
  query_len = 0; query[0] = 0; reload();
}
void open_selected() {
  if (selected < 0 || selected >= count || !matches(selected)) return;
  char absolute[1024]; fullpath(entries[selected].name, absolute, sizeof absolute);
  if (entries[selected].directory) {
    if (strlen(absolute) >= sizeof path) { snprintf(notice, sizeof notice, "Слишком длинный путь"); return; }
    snprintf(path, sizeof path, "%s", absolute); query_len = 0; query[0] = 0; reload(); return;
  }
  int fd = open(absolute, O_RDONLY);
  if (fd < 0) { snprintf(notice, sizeof notice, "Ошибка чтения: %d", errno); return; }
  ssize_t n = read(fd, preview, sizeof preview - 1); close(fd);
  if (n < 0) { snprintf(notice, sizeof notice, "Ошибка чтения: %d", errno); return; }
  preview[n] = 0;
  for (ssize_t i = 0; i < n; ++i) if ((static_cast<unsigned char>(preview[i]) < 32 && preview[i] != '\n' && preview[i] != '\t' && preview[i] != '\r')) {
    snprintf(preview, sizeof preview, "Двоичный файл.\nТекстовый просмотр недоступен."); break;
  }
  layout_preview(); viewing = true; printf("files: preview %s\n", absolute);
}
void draw(uint32_t* pixels, bool bgrx) {
  canvas.target(pixels, kWidth, kHeight, bgrx);
  canvas.fill(0, 0, kWidth, kHeight, 0x121214);
  canvas.fill(0, 0, kWidth, 56, 0x141416);
  canvas.fill(0, 55, kWidth, 1, 0x2a2a2e);
  if (hover == 1000) canvas.rounded(10, 10, 78, 36, 8, 0x29251b);
  if (hover == 1001) canvas.rounded(92, 10, 114, 36, 8, 0x242426);
  canvas.text("Назад", 18, 34, 0xf5c518, 84);
  canvas.text("Обновить", 100, 34, 0xf3efe7, 206);
  canvas.text(path, 218, 34, 0xb7b1a6, 610, 13, true);
  canvas.rounded(630, 10, 214, 36, 9, search ? 0x332e25 : 0x0b0b0c);
  canvas.text(query_len == 0 ? "Поиск ( / )" : query, 642, 34, 0xb7b1a6, 834, 13);
  canvas.fill(0, 56, 174, kHeight - 56, 0x101012);
  canvas.fill(173, 56, 1, kHeight - 56, 0x1e1e21);
  canvas.text("МЕСТА", 20, 88, 0x8e887d, 160, 11);
  const char* places[] = {"/", "/bin", "/etc"};
  const char* labels[] = {"Корень /", "Программы", "Настройки"};
  for (int i = 0; i < 3; ++i) {
    bool active = strcmp(path, places[i]) == 0;
    if (active) canvas.rounded(10, 102 + i * 38, 154, 32, 8, 0x29251b);
    else if (hover == 1010 + i) canvas.rounded(10, 102 + i * 38, 154, 32, 8, 0x202022);
    canvas.text(labels[i], 20, 126 + i * 38, active ? 0xf5c518 : 0xd8d2c7, 160);
  }
  canvas.text("ПРОСМОТР", 20, 425, 0x8e887d, 160, 11);
  if (viewing) {
    canvas.text(entries[selected].name, 194, 88, 0xf5c518, 840, 16);
    char line[320];
    for (int row = 0; row < 16 && preview_first + row < preview_count; ++row) {
      const char* p = preview + preview_lines[preview_first + row];
      const char* end = preview_first + row + 1 < preview_count ? preview + preview_lines[preview_first + row + 1] : preview + strlen(preview);
      int n = 0;
      while (p < end && *p != '\n' && n < 318) { if (*p != '\r') line[n++] = *p == '\t' ? ' ' : *p; ++p; }
      line[n] = 0;
      canvas.text(line, 194, 120 + row * 20, 0xd8d2c7, 840, 13, true);
    }
    if (preview_count > 16) {
      canvas.fill(851, 100, 4, 320, 0x1e1e21);
      canvas.fill(851, 100 + preview_first * 320 / preview_count, 4, 5120 / preview_count, 0x8e887d);
    }
  } else {
    canvas.text("Имя", 210, 86, 0x8e887d, 590, 12);
    canvas.text("Тип", 620, 86, 0x8e887d, 720, 12);
    canvas.text("Байт", 754, 86, 0x8e887d, 846, 12);
    int row = 0;
    for (; row < 10; ++row) {
      int i = index_at(first + row);
      if (i < 0) break;
      int y = 100 + row * 32;
      if (i == selected) canvas.rounded(186, y, 660, 30, 7, 0x29251b);
      else if (hover == row) canvas.rounded(186, y, 660, 30, 7, 0x202022);
      canvas.fill(196, y + 11, 10, 10, entries[i].directory ? 0xf5c518 : 0x8e887d);
      canvas.text(entries[i].name, 218, y + 21, 0xf3efe7, 600);
      canvas.text(entries[i].directory ? "Папка" : "Файл", 620, y + 21, 0x8e887d, 730, 12);
      if (!entries[i].directory && entries[i].size >= 0) {
        char size[32]; snprintf(size, sizeof size, "%lld", entries[i].size);
        canvas.text(size, 754, y + 21, 0xb7b1a6, 844, 12, true);
      }
    }
    int found = matching_count();
    if (found > 10) {
      canvas.fill(851, 100, 4, 320, 0x1e1e21);
      canvas.fill(851, 100 + first * 320 / found, 4, 3200 / found, 0x8e887d);
    }
    if (row == 0) canvas.text("Ничего не найдено", 214, 150, 0x8e887d, 820);
  }
  canvas.fill(174, 448, kWidth - 174, 1, 0x1e1e21);
  char status[128];
  if (viewing) snprintf(status, sizeof status, "Строки %d–%d / %d  ·  до 4 КиБ  ·  ↑↓ PgUp/PgDn", preview_count ? preview_first + 1 : 0, preview_first + 16 < preview_count ? preview_first + 16 : preview_count, preview_count);
  else if (query_len) snprintf(status, sizeof status, "Найдено %d из %d%s  ·  Enter: открыть", matching_count(), count, truncated ? "+" : "");
  else snprintf(status, sizeof status, "%d%s элементов  ·  Enter: открыть  ·  Backspace: назад", count, truncated ? "+" : "");
  canvas.text(notice[0] == 0 ? status : notice, 194, 470, notice[0] == 0 ? 0x8e887d : 0xffb5ac, 844, 11);
}
bool input(const sc::InputEvent& e) {
  if (e.type == sc::kInputWheel && viewing && e.value != 0) { scroll_preview(e.value > 0 ? -3 : 3); return true; }
  if (e.type == sc::kInputWheel && !viewing && e.value != 0) {
    int next = first - (e.value > 0 ? 3 : -3);
    int maximum = matching_count() > 10 ? matching_count() - 10 : 0;
    if (next < 0) next = 0;
    if (next > maximum) next = maximum;
    first = next; return true;
  }
  if (e.type == sc::kInputMotion) { pointer_x = e.x; pointer_y = e.y; int next = hit(e.x, e.y); bool changed = next != hover; hover = next; return changed; }
  if (e.type == sc::kInputButton && e.code == 1 && e.value == 1) {
    pointer_x = e.x; pointer_y = e.y;
    search = pointer_y >= 10 && pointer_y < 46 && pointer_x >= 630 && pointer_x < 844;
    if (pointer_y < 56) {
      if (pointer_x < 88) parent();
      else if (pointer_x < 206) reload();
      else if (pointer_x >= 630) { search = true; viewing = false; }
    } else if (pointer_x < 174) {
      if (pointer_y >= 102 && pointer_y < 220) {
        const char* places[] = {"/", "/bin", "/etc"};
        int index = (pointer_y - 102) / 38;
        if (index < 3) { snprintf(path, sizeof path, "%s", places[index]); query_len = 0; query[0] = 0; reload(); }
      }
    } else if (!viewing && pointer_y >= 100 && pointer_y < 420) {
      if (pointer_x >= 848 && matching_count() > 10) {
        first = (pointer_y - 100) * (matching_count() - 10) / 320;
        return true;
      }
      int row = (pointer_y - 100) / 32;
      int i = index_at(first + row);
      if (i >= 0) { if (selected == i) open_selected(); else selected = i; }
    }
    return true;
  }
  if (e.type == sc::kInputKey && (e.code == 0xe1 || e.code == 0xe5)) {
    unsigned bit = e.code == 0xe1 ? 1u : 2u;
    if (e.value != 0) shift_keys |= bit; else shift_keys &= ~bit;
    return false;
  }
  if (e.type != sc::kInputKey || e.value == 0) return false;
  if (e.code == 0x38 && !search) { search = true; viewing = false; return true; }
  if (search) {
    if (e.code == 0x29 || e.code == 0x28) search = false;
    else if (e.code == 0x2a && query_len > 0) query[--query_len] = 0;
    else if (query_len < 63) {
      char character = 0;
      if (e.code >= 4 && e.code <= 29) character = static_cast<char>((shift_keys != 0 ? 'A' : 'a') + e.code - 4);
      else if (e.code >= 0x1e && e.code <= 0x27) character = (shift_keys != 0 ? "!@#$%^&*()" : "1234567890")[e.code - 0x1e];
      else if (e.code == 0x2c) character = ' ';
      else if (e.code >= 0x2d && e.code <= 0x38) character = (shift_keys != 0 ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[e.code - 0x2d];
      if (character != 0) { query[query_len++] = character; query[query_len] = 0; }
    }
    first = 0;
    selected = index_at(0);
    return true;
  }
  // A preview keeps its original entry; list navigation must not change the
  // heading while leaving bytes from a different file on screen.
  if (viewing) {
    if (e.code == 0x2a || e.code == 0x29) { parent(); return true; }
    if (e.code == 0x51 || e.code == 0x52 || e.code == 0x4e || e.code == 0x4b || e.code == 0x4a || e.code == 0x4d) {
      scroll_preview(e.code == 0x51 ? 1 : e.code == 0x52 ? -1 : e.code == 0x4e ? 16 : e.code == 0x4b ? -16 : e.code == 0x4a ? -512 : 512);
      return true;
    }
    return false;
  }
  if (e.code == 0x28) open_selected();
  else if (e.code == 0x2a || e.code == 0x29) parent();
  else if (e.code == 0x51 || e.code == 0x52) {
    int step = e.code == 0x51 ? 1 : -1;
    for (int i = selected + step; i >= 0 && i < count; i += step) if (matches(i)) { selected = i; break; }
    int ordinal = 0;
    for (int i = 0; i < selected; ++i) if (matches(i)) ++ordinal;
    if (ordinal < first) first = ordinal;
    if (ordinal >= first + 10) first = ordinal - 9;
  } else return false;
  return true;
}
}

extern "C" int main(int argc, char** argv) {
  if (argc > 1 && strlen(argv[1]) < sizeof path) snprintf(path, sizeof path, "%s", argv[1]);
  if (!canvas.fonts({wm_ui_font, static_cast<size_t>(wm_ui_font_end - wm_ui_font)},
                    {wm_mono_font, static_cast<size_t>(wm_mono_font_end - wm_mono_font)})) return 1;
  charm::gui::Window window;
  if (window.create("Файлы", kWidth, kHeight) != charm::Status::Ok) return 1;
  reload(); int available = 0; bool dirty = true;
  for (;;) {
    if (dirty && available >= 0) {
      draw(window.pixels(static_cast<uint32_t>(available)), window.format() == sc::kDisplayBgrx);
      if (window.present(static_cast<uint32_t>(available), {0, 0, kWidth, kHeight}) != charm::Status::Ok) return 1;
      available = -1; dirty = false;
    }
    wm::WindowEvent event{};
    auto status = window.wait(&event);
    if (status == charm::Status::Interrupted) continue;
    if (status != charm::Status::Ok || event.type == wm::kCloseRequest) return 0;
    if (event.type == wm::kFrameDone) available = static_cast<int>(event.value);
    if (event.type == wm::kInput) {
      sc::InputEvent e{}; while (window.next_input(&e)) if (input(e)) dirty = true;
    }
  }
}
