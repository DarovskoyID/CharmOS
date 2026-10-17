#pragma once
#include <wchar.h>
#include <string.h>

static void unicode_checks() {
  mbstate_t state{};
  char encoded[5]{};
  wchar_t decoded = 0;
  // Exhaust all Unicode scalars, including boundary and noncharacter values.
  for (uint32_t c = 0; c <= 0x10ffff; ++c) {
    size_t n = wcrtomb(encoded, static_cast<wchar_t>(c), &state);
    if (c >= 0xd800 && c <= 0xdfff) {
      CHECK(n == static_cast<size_t>(-1) && errno == EILSEQ && mbsinit(&state));
      continue;
    }
    CHECK(n >= 1 && n <= 4);
    CHECK(mbrtowc(&decoded, encoded, n, &state) == (c ? n : 0));
    CHECK(static_cast<uint32_t>(decoded) == c && mbsinit(&state));
  }
  const char* invalid[] = {"\xc0\x80", "\xe0\x80\x80", "\xed\xa0\x80", "\xf0\x80\x80\x80",
                           "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\x80", "\xe2!", "\xc2"};
  for (auto s : invalid) {
    CHECK(mbrtowc(&decoded, s, strlen(s) + 1, &state) == static_cast<size_t>(-1));
    CHECK(errno == EILSEQ && mbsinit(&state));
  }
  const char* text = "А😀";
  CHECK(mbrtowc(&decoded, text, 1, &state) == static_cast<size_t>(-2));
  CHECK(!mbsinit(&state));
  CHECK(mbrtowc(&decoded, text + 1, 1, &state) == 1 && decoded == L'А');
  for (size_t i = 2; i < 5; ++i)
    CHECK(mbrtowc(&decoded, text + i, 1, &state) == static_cast<size_t>(-2));
  CHECK(mbrtowc(&decoded, text + 5, 1, &state) == 1 && decoded == 0x1f600);
  const char* input = text;
  CHECK(mbsrtowcs(nullptr, &input, 0, &state) == 2 && input == text && mbsinit(&state));
  wchar_t wide[4]{};
  CHECK(mbsrtowcs(wide, &input, 1, &state) == 1 && input == text + 2);
  CHECK(mbsrtowcs(wide + 1, &input, 3, &state) == 1 && input == nullptr);
  const wchar_t* source = wide;
  CHECK(wcsrtombs(nullptr, &source, 0, &state) == 6 && source == wide);
  char bytes[8]; memset(bytes, 0x55, sizeof bytes);
  CHECK(wcsrtombs(bytes, &source, 3, &state) == 2 && source == wide + 1 && bytes[2] == 0x55);
  CHECK(wcsrtombs(bytes + 2, &source, 5, &state) == 4 && source == nullptr);
  CHECK(strcmp(bytes, text) == 0);
  wchar_t move[] = L"абвг";
  wmemmove(move + 1, move, 3);
  CHECK(wcscmp(move, L"аабв") == 0);
  wmemmove(move, move + 1, 3); move[3] = 0;
  CHECK(wcscmp(move, L"абв") == 0);
  CHECK(wcsstr(move, L"бв") == move + 1 && wcschr(move, 0) == move + 3);
  printf("threads runtime: exhaustive UTF-8 scalars and streaming ok\n");
}
