// CharmOS Blink font fallback.
//
// The platform has a directory font manager rooted at /fonts. Character
// fallback asks Skia to match the requested family/style/BCP47 list and then
// resolves the returned family through Blink's normal font platform data path.
#include "skia/ext/font_utils.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/font_face_creation_params.h"
#include "third_party/blink/renderer/platform/fonts/font_platform_data.h"
#include "third_party/blink/renderer/platform/fonts/simple_font_data.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace blink {

namespace {

AtomicString& MutableSystemFontFamily() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(AtomicString, system_font_family, ());
  return system_font_family;
}

}  // namespace

const AtomicString& FontCache::SystemFontFamily() {
  AtomicString& family = MutableSystemFontFamily();
  if (family.empty()) family = AtomicString("sans-serif");
  return family;
}

void FontCache::SetSystemFontFamily(const AtomicString& family_name) {
  DCHECK(!family_name.empty());
  MutableSystemFontFamily() = family_name;
}

const SimpleFontData* FontCache::PlatformFallbackFontForCharacter(
    const FontDescription& font_description,
    UChar32 c,
    const SimpleFontData* /*font_data_to_substitute*/,
    FontFallbackPriority /*fallback_priority*/) {
  sk_sp<SkFontMgr> manager = skia::DefaultFontMgr();
  if (!manager) return nullptr;

  const SkFontStyle style(
      font_description.Weight() >= kBoldThreshold ? SkFontStyle::kBold_Weight
                                                  : SkFontStyle::kNormal_Weight,
      SkFontStyle::kNormal_Width,
      font_description.Style() == kItalicSlopeValue ? SkFontStyle::kItalic_Slant
                                                    : SkFontStyle::kUpright_Slant);
  const auto locale = font_description.LocaleOrDefault().Ascii();
  const char* languages[] = {locale.data()};
  const char* family = "";
  sk_sp<SkTypeface> typeface = manager->matchFamilyStyleCharacter(
      family, style, languages, locale.length() ? 1 : 0, c);
  if (!typeface) return nullptr;

  SkString family_name;
  typeface->getFamilyName(&family_name);
  if (family_name.isEmpty()) return nullptr;

  const FontFaceCreationParams creation_params(
      AtomicString(family_name.c_str()));
  const FontPlatformData* platform_data =
      GetFontPlatformData(font_description, creation_params);
  if (!platform_data) return nullptr;
  return FontDataFromFontPlatformData(platform_data);
}

}  // namespace blink
