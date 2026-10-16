#pragma once
#include <stddef.h>
#include <stdint.h>
namespace charm::ui {
inline size_t encode(uint32_t cp, char* out) {
  if (cp < 0x80) { out[0] = static_cast<char>(cp); return 1; }
  if (cp < 0x800) { out[0] = static_cast<char>(0xc0 | (cp >> 6)); out[1] = static_cast<char>(0x80 | (cp & 63)); return 2; }
  out[0] = static_cast<char>(0xe0 | (cp >> 12)); out[1] = static_cast<char>(0x80 | ((cp >> 6) & 63)); out[2] = static_cast<char>(0x80 | (cp & 63)); return 3;
}
inline bool append(char* text, size_t capacity, unsigned& length, uint32_t cp) {
  if (!cp || cp > 0xffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  char bytes[3]; size_t n = encode(cp, bytes);
  if (length + n >= capacity) return false;
  for (size_t i = 0; i < n; ++i) text[length++] = bytes[i];
  text[length] = 0; return true;
}
inline void backspace(char* text, unsigned& length) {
  if (!length) return;
  do { --length; } while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80);
  text[length] = 0;
}
inline uint32_t key(uint32_t code, bool shift, bool russian) {
  if (code >= 4 && code <= 29) {
    constexpr uint32_t ru[] = {0x444,0x438,0x441,0x432,0x443,0x430,0x43f,0x440,0x448,0x43e,0x43b,0x434,0x44c,0x442,0x449,0x437,0x439,0x43a,0x44b,0x435,0x433,0x43c,0x446,0x447,0x43d,0x44f};
    uint32_t c = russian ? ru[code - 4] : 'a' + code - 4;
    return shift ? c - (russian ? 0x20 : 32) : c;
  }
  if (code >= 0x1e && code <= 0x27) return static_cast<unsigned char>((shift ? "!@#$%^&*()" : "1234567890")[code - 0x1e]);
  if (code == 0x2c) return ' ';
  if (russian) {
    uint32_t c = code == 0x2f ? 0x445 : code == 0x30 ? 0x44a : code == 0x33 ? 0x436 : code == 0x34 ? 0x44d : code == 0x35 ? 0x451 : code == 0x36 ? 0x431 : code == 0x37 ? 0x44e : 0;
    if (c) return shift ? (c == 0x451 ? 0x401 : c - 0x20) : c;
  }
  if (code >= 0x2d && code <= 0x38) return static_cast<unsigned char>((shift ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[code - 0x2d]);
  return 0;
}
}
