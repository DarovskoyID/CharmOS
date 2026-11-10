// CharmOS font fallback and render parameters.
//
// CharmOS installs a directory font manager over /fonts, so character fallback
// asks Skia's font manager to match the requested style and code point and
// reports the resulting family. Rendering uses the defaults built into
// FontRenderParams (grayscale antialiasing, medium hinting).
#include <string>
#include <string_view>
#include <vector>

#include "skia/ext/font_utils.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_fallback.h"
#include "ui/gfx/font_render_params.h"

namespace gfx {

std::vector<Font> GetFallbackFonts(const Font& /*font*/) {
  return {};
}

bool GetFallbackFont(const Font& font,
                     const std::string& locale,
                     std::u16string_view text,
                     Font* result) {
  if (text.empty() || !result) return false;
  sk_sp<SkFontMgr> manager = skia::DefaultFontMgr();
  if (!manager) return false;
  const SkFontStyle style(
      font.GetWeight() >= Font::Weight::BOLD ? SkFontStyle::kBold_Weight
                                             : SkFontStyle::kNormal_Weight,
      SkFontStyle::kNormal_Width,
      font.GetStyle() & Font::ITALIC ? SkFontStyle::kItalic_Slant
                                     : SkFontStyle::kUpright_Slant);
  const char* languages[] = {locale.c_str()};
  sk_sp<SkTypeface> typeface = manager->matchFamilyStyleCharacter(
      font.GetFontName().c_str(), style, languages, locale.empty() ? 0 : 1,
      static_cast<SkUnichar>(text[0]));
  if (!typeface) return false;
  SkString family;
  typeface->getFamilyName(&family);
  if (family.isEmpty()) return false;
  *result = Font(std::string(family.c_str()), font.GetFontSize());
  return true;
}

FontRenderParams GetFontRenderParams(const FontRenderParamsQuery& query,
                                     std::string* family_out) {
  if (family_out && !query.families.empty()) *family_out = query.families.front();
  return FontRenderParams();
}

void ClearFontRenderParamsCache() {}

bool GetFontRenderParamsSubpixelRenderingEnabled() {
  return false;
}

namespace {
float g_device_scale_factor = 1.0f;
}  // namespace

float GetFontRenderParamsDeviceScaleFactor() {
  return g_device_scale_factor;
}

void SetFontRenderParamsDeviceScaleFactor(float device_scale_factor) {
  g_device_scale_factor = device_scale_factor;
}

}  // namespace gfx
