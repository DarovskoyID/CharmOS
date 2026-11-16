// Character fallback for SkFontMgr_Custom_Directory. Its upstream
// onMatchFamilyStyleCharacter is deliberately unimplemented (always nullptr).
#ifndef BUILD_CHARMOS_FONT_FALLBACK_CHARMOS_H_
#define BUILD_CHARMOS_FONT_FALLBACK_CHARMOS_H_

#include <string_view>
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace charmos {

inline SkUnichar FirstScalar(std::u16string_view text) {
  if (text.empty()) return -1;
  const unsigned first = text[0];
  if (first >= 0xd800 && first <= 0xdbff) {
    if (text.size() < 2 || text[1] < 0xdc00 || text[1] > 0xdfff) return -1;
    return 0x10000 + ((first - 0xd800) << 10) + (text[1] - 0xdc00);
  }
  return first >= 0xdc00 && first <= 0xdfff ? -1 : first;
}

inline sk_sp<SkTypeface> FontForCharacter(SkFontMgr* manager,
                                         const char* preferred_family,
                                         const SkFontStyle& style,
                                         SkUnichar character) {
  if (!manager || character < 0 || character > 0x10ffff ||
      (character >= 0xd800 && character <= 0xdfff)) return nullptr;
  auto match = [&](const char* family) -> sk_sp<SkTypeface> {
    auto face = manager->matchFamilyStyle(family, style);
    return face && face->unicharToGlyph(character) ? face : nullptr;
  };
  if (preferred_family && *preferred_family) {
    if (auto face = match(preferred_family)) return face;
  }
  for (int i = 0; i < manager->countFamilies(); ++i) {
    SkString family;
    manager->getFamilyName(i, &family);
    if (auto face = match(family.c_str())) return face;
  }
  return nullptr;  // Never claim coverage when only the missing glyph exists.
}

}  // namespace charmos
#endif
