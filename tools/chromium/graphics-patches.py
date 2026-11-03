"""Pinned C++ Skia backend selection for native software rendering."""

def graphics_patches():
    return {
        "skia/BUILD.gn": (
            "b39f03ab91d2f816b1237a5b3c218c6ea3a51b025ecb60fac8b3f65bd2aaad62", [
                ('source_set("path_bridge") {', 'if (!is_charmos) {\nsource_set("path_bridge") {'),
                ('# External-facing config for dependent code.', '}  # !is_charmos\n\n# External-facing config for dependent code.'),
                ('    "SK_EXIF_PARSE_WITH_RUST",\n  ]', '''    "SK_EXIF_PARSE_WITH_RUST",
  ]
  if (is_charmos) {
    defines -= [
      "SK_USE_PARTITION_ALLOC",
      "SK_CODEC_DECODES_PNG_WITH_RUST",
      "SK_CODEC_ENCODES_PNG_WITH_RUST",
      "SK_CODEC_DECODES_BMP_WITH_RUST",
      "SK_CODEC_DECODES_ICO_WITH_RUST",
      "SK_CODEC_COLOR_PROFILE_PARSE_WITH_RUST",
      "SK_EXIF_PARSE_WITH_RUST",
    ]
    defines += [ "SK_CODEC_DECODES_BMP", "SK_CODEC_DECODES_ICO" ]
  }'''),
                ('  if (use_blink) {\n    defines += [ "SK_TYPEFACE_FACTORY_FONTATIONS" ]',
                 '  if (use_blink && !is_charmos) {\n    defines += [ "SK_TYPEFACE_FACTORY_FONTATIONS" ]'),
                ('  public += skia_codec_rust_png_public', '''  if (is_charmos) {
    public += skia_codec_libpng_public
    public += skia_encode_libpng_public
  } else {
    public += skia_codec_rust_png_public
  }'''),
                ('  public += skia_encode_rust_png_public', '  if (!is_charmos) {\n    public += skia_encode_rust_png_public\n  }'),
                ('  sources += skia_codec_rust_png', '''  if (is_charmos) {
    sources += skia_codec_libpng_srcs
    sources += [ "//third_party/skia/src/codec/SkIcoCodec.cpp" ]
    public += [
      "//third_party/skia/include/codec/SkBmpDecoder.h",
      "//third_party/skia/include/codec/SkIcoDecoder.h",
    ]
    sources -= [ "ext/codec_utils.cc" ]
    sources += [ "//build/charmos/codec_utils_charmos.cc" ]
    deps += [ "//third_party/libpng" ]
  } else {
    sources += skia_codec_rust_png
  }'''),
                ('  sources += skia_encode_rust_png_srcs', '''  if (is_charmos) {
    sources += skia_encode_libpng_srcs
  } else {
    sources += skia_encode_rust_png_srcs
  }'''),
                ('    sources += skia_ports_typeface_fontations_sources', '''    if (!is_charmos) {
    sources += skia_ports_typeface_fontations_sources'''),
                ('  if (is_win) {\n    # See SK_TYPEFACE_FACTORY_DIRECTWRITE', '''    }  # use_blink
    if (use_blink && is_charmos) {
      sources += skia_ports_fontmgr_directory_sources
      public += skia_ports_fontmgr_directory_public
    }
  if (is_win) {
    # See SK_TYPEFACE_FACTORY_DIRECTWRITE'''),
                ('    ":rust_common_ffi",\n    ":rust_png_ffi",\n', ''),
                ('    ":rust_bmp_ffi",\n    ":rust_exif_ffi",\n    ":rust_icc_ffi",\n    ":rust_ico_ffi",\n    ":rust_jpeg_ffi",\n', ''),
                ('  if (use_blink) {\n    deps += [ "//third_party:jpeg" ]', '''  if (!is_charmos) {
    deps += [ ":rust_common_ffi", ":rust_png_ffi" ]
    public_deps += [ ":rust_bmp_ffi", ":rust_exif_ffi", ":rust_icc_ffi", ":rust_ico_ffi", ":rust_jpeg_ffi" ]
  }
  if (use_blink) {
    deps += [ "//third_party:jpeg" ]'''),
                ('rust_static_library("rust_common_ffi") {', 'if (!is_charmos) {\nrust_static_library("rust_common_ffi") {'),
                ('# Template for things that are logically part of :skia,', '}  # !is_charmos Rust codecs\n\n# Template for things that are logically part of :skia,'),
                ('  if (use_blink) {\n    # Fontations Rust/C++ bridge interfaces, needed by SkTypeface.',
                 '  if (use_blink && !is_charmos) {\n    # Fontations Rust/C++ bridge interfaces, needed by SkTypeface.'),
                ('group("test_fonts_resources") {', 'if (!is_charmos) {\ngroup("test_fonts_resources") {'),
                ('group("fuzzers") {\n  deps = [ "//skia/tools/fuzzers" ]\n}', 'group("fuzzers") {\n  deps = [ "//skia/tools/fuzzers" ]\n}\n}  # native integration tests live in build/charmos'),
                ('    defines += [ "SK_CODEC_DECODES_BMP", "SK_CODEC_DECODES_ICO" ]',
                 '    defines += [ "SK_CODEC_DECODES_BMP", "SK_CODEC_DECODES_ICO" ]\n    defines += [ "SK_GAMMA_EXPONENT=1.2", "SK_GAMMA_CONTRAST=0.2" ]'),
                ('  if (use_blink) {\n    deps += [ "//third_party:jpeg" ]', '''  if (is_charmos) {
    # Compile the real features without loading unrelated UI service roots.
    deps -= [ "//ui/base:features" ]
    deps += [ "//build/charmos:ui_features" ]
  }
  if (use_blink) {
    deps += [ "//third_party:jpeg" ]'''),
                ('  sources += skia_utils_private', '''  sources += skia_utils_private
  if (is_charmos) {
    sources = filter_exclude(sources, [ "*/mac/*", "*/win/*" ])
  }'''),
                ('    defines += [ "SK_GAMMA_EXPONENT=1.2", "SK_GAMMA_CONTRAST=0.2" ]',
                 '    defines += [ "SK_GAMMA_EXPONENT=1.2", "SK_GAMMA_CONTRAST=0.2", "SK_BUILD_FOR_UNIX" ]'),
                ('    sources = filter_exclude(sources, [ "*/mac/*", "*/win/*" ])',
                 '    sources -= filter_include(sources, [ "*/mac/*", "*/win/*" ])'),
            ]),
        "skia/features.gni": (
            "b87d0ec8ae38c8649ff71b7b1616fef83506df4ca3348cc1349f8ef74b2edb0a", [
                ('skia_support_gpu = use_blink', 'skia_support_gpu = use_blink && !is_charmos'),
                ('skia_use_gl = true', 'skia_use_gl = !is_charmos'),
            ]),
        "third_party/libpng/visibility.gni": (
            "4b1370f021d86d001bf53a903a4ad6eb1e907559b6c042ce1d3701b9aefd4c39", [
                ('visibility = []', 'visibility = []\nif (is_charmos) {\n  visibility += [ "//skia:skia" ]\n}'),
            ]),
        "skia/ext/font_utils.cc": (
            "2b535e7bfcd11e3610c0850ab73c617568e469bc1b9b761c1b9363c549406f9f", [
                ('#include <mutex>', '#if BUILDFLAG(IS_CHARMOS)\n#include "third_party/skia/include/ports/SkFontMgr_directory.h"\n#endif\n#include <mutex>'),
                ('#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)',
                 '#elif BUILDFLAG(IS_CHARMOS)\n  return SkFontMgr_New_Custom_Directory("/fonts");\n#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)'),
            ]),
        "third_party/harfbuzz/BUILD.gn": (
            "ab62f7d988f46e9d11756d6c261ce1ac73707f13edcf6dea0c0cd08629d97d67", [
                ('  rust_bindgen_generator("hb_rs_generator") {', '  if (!is_charmos) {\n  rust_bindgen_generator("hb_rs_generator") {'),
                ('  config("harfbuzz_config") {', '  }  # !is_charmos\n\n  config("harfbuzz_config") {'),
                ('    public_configs = [ ":harfbuzz_config" ]\n\n    deps = [', '''    public_configs = [ ":harfbuzz_config" ]
    if (is_charmos) {
      defines -= [ "HAVE_FONTATIONS", "HAVE_HARFRUST" ]
    }

    deps = ['''),
                ('      ":harfbuzz_rust",\n      "//third_party/icu:icuuc",\n      "//third_party/rust/harfrust/v0_13:lib",', '      "//third_party/icu:icuuc",'),
                ('    if (use_glib) {', '''    if (!is_charmos) {
      deps += [ ":harfbuzz_rust", "//third_party/rust/harfrust/v0_13:lib" ]
    }
    if (use_glib) {'''),
            ]),
        "third_party/libjpeg_turbo/BUILD.gn": (
            "e1d6fdfcfca57a2b6fc7138a7d1b1b1595e5206ac4acf2a6ff28f2e7bd2b948f", [
                ('if (build_with_chromium) {', 'if (build_with_chromium && !is_charmos) {'),
            ]),
        "skia/ext/SkMemory_new_handler.cpp": (
            "c7730069c0e1dd883c1275ee4e80bf07d98761dac57f1b7da606096ab11be732", [
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHARMOS)'),
            ]),
    }
