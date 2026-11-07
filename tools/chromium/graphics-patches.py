"""Pinned C++ Skia backend selection for native software rendering."""

def desktop_assert(digest, android=True):
    before = 'assert(is_win || is_mac || is_linux || is_chromeos'
    after = before + (' || is_android' if android else '') + ' || is_charmos)'
    before += (' || is_android' if android else '') + ')'
    return digest, [(before, after)]

def graphics_patches():
    patches = {
        "components/cbor/BUILD.gn": (
            "4cc274711c3b0a614a5e7385cb065385c31797ab03e337c599ddf9c6b8d66b49", [
                ('  flags = [ "USE_CBOR_RUST=!$is_cronet_build" ]',
                 '  if (charmos_repo != "") {\n    flags = [ "USE_CBOR_RUST=false" ]\n  } else {\n    flags = [ "USE_CBOR_RUST=!$is_cronet_build" ]\n  }'),
                ('  if (!is_cronet_build) {\n    public_deps = [',
                 '  if (!is_cronet_build && charmos_repo == "") {\n    public_deps = ['),
            ]),
        "crypto/BUILD.gn": (
            "229a47c328acf0341894de2785cbd34764e7c9b1ed9f6d20f3d87e6b90dfcd1a", [
                ('  deps = [\n    ":tpm",\n    "//components/cbor",\n  ]',
                 '  deps = [ "//components/cbor" ]\n  if (is_charmos) {\n    sources -= [ "tpm_parser.cc", "tpm_parser.h", "unexportable_key_software_unsecure.cc" ]\n    sources += [ "//build/charmos/unexportable_key_charmos.cc" ]\n  } else {\n    deps += [ ":tpm" ]\n  }'),
            ]),
        "base/i18n/icubridge/icu_bridge.cc": (
            "1839d362522cbfa4d7420fedcb6a4d175b4a9a4462dbb99a30ce1a82f4185fbf", [
                ('  return base::FeatureList::IsEnabled(kUseIcu4xNormalizer) ? *icu4x_normalizer_\n                                                           : *icu4c_normalizer_;',
                 '#if BUILDFLAG(IS_CHARMOS)\n  return *icu4c_normalizer_;\n#else\n  return base::FeatureList::IsEnabled(kUseIcu4xNormalizer) ? *icu4x_normalizer_\n                                                           : *icu4c_normalizer_;\n#endif'),
            ]),
        "net/BUILD.gn": (
            "44dd56e72df3c638402d0faa5c198e5981170931c6b641e06ef29668752106c4", [
                ('    "//third_party/rust/sfv/v0_15/wrapper",\n', ''),
                ('  public_deps = [\n    ":disk_cache_buildflags",',
                 '  if (!is_charmos) {\n    deps += [ "//third_party/rust/sfv/v0_15/wrapper" ]\n  }\n  public_deps = [\n    ":disk_cache_buildflags",'),
            ]),
        "net/features.gni": (
            "d201387125238678d5df71034d5c3376d1deb27fb9a2f9c946814ea3b76a39d1", [
                ('  use_kerberos = !is_ios && !is_fuchsia && !is_castos && !is_cast_android',
                 '  use_kerberos = !is_ios && !is_fuchsia && !is_castos && !is_cast_android && !is_charmos'),
            ]),
        "net/cert/cert_verify_proc.cc": (
            "b820ab2c46df0dac3d8502b0e388aea2be1aee40fb0b85e0c2fd3b6bfc91db08", [
                ('#if !(BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(CHROME_ROOT_STORE_ONLY))',
                 '#if !(BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(CHROME_ROOT_STORE_ONLY) || BUILDFLAG(IS_CHARMOS))'),
            ]),
        "components/variations/client_filterable_state.cc": (
            "ab33f0956fdbc3a2292e80fe031c1e67db96c9224cb8c57c7f97943c8f5368cc", [
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS)',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS) || BUILDFLAG(IS_CHARMOS)'),
            ]),
        "net/cert/internal/system_trust_store.h": (
            "96a46233aa73719f0f4aed658521f752116d252d0192661214420e472a5afec2", [
                ('#if BUILDFLAG(IS_FUCHSIA)\n// Creates an instance of SystemTrustStore that wraps the current platform\'s SSL',
                 '#if BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n// Creates an instance of SystemTrustStore that wraps the current platform\'s SSL'),
            ]),
        "net/cert/cert_verifier.cc": (
            "00d7d9124bc333acda1f2a24f4fe302ea0ce114e7b55cdca9609ec8b76bc098b", [
                ('#elif BUILDFLAG(IS_FUCHSIA)\n    return CertVerifyProc::CreateBuiltinVerifyProc(',
                 '#elif BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n    return CertVerifyProc::CreateBuiltinVerifyProc('),
            ]),
        "net/http/structured_headers.cc": (
            "ff8723693e670ada2264f36b03c79227dfeb47df6b7c07fae509c59f37a4d312", [
                ('#include "base/strings/string_view_rust.h"',
                 '#include "base/time/time.h"\n#include "build/build_config.h"\n#if !BUILDFLAG(IS_CHARMOS)\n#include "base/strings/string_view_rust.h"'),
                ('}  // namespace sfv\n', '}  // namespace sfv\n#endif  // !IS_CHARMOS Rust bridge\n'),
                ('std::optional<ParameterizedItem> ParseItem(std::string_view str) {',
                 'std::optional<ParameterizedItem> ParseItem(std::string_view str) {\n#if !BUILDFLAG(IS_CHARMOS)'),
                ('  return ParseAndRecordMetrics(kTimeMetricItem, kSuccessMetricItem, [&]() {',
                 '#endif\n  return ParseAndRecordMetrics(kTimeMetricItem, kSuccessMetricItem, [&]() {'),
                ('std::optional<List> ParseList(std::string_view str) {',
                 'std::optional<List> ParseList(std::string_view str) {\n#if !BUILDFLAG(IS_CHARMOS)'),
                ('  return ParseAndRecordMetrics(kTimeMetricList, kSuccessMetricList, [&]() {',
                 '#endif\n  return ParseAndRecordMetrics(kTimeMetricList, kSuccessMetricList, [&]() {'),
                ('std::optional<Dictionary> ParseDictionary(std::string_view str) {',
                 'std::optional<Dictionary> ParseDictionary(std::string_view str) {\n#if !BUILDFLAG(IS_CHARMOS)'),
                ('  return ParseAndRecordMetrics(\n      kTimeMetricDictionary, kSuccessMetricDictionary,',
                 '#endif\n  return ParseAndRecordMetrics(\n      kTimeMetricDictionary, kSuccessMetricDictionary,'),
            ]),
        "ui/events/keycodes/dom/keycode_converter.cc": (
            "78d06ca83894c014fc88fc60eb275c2786481810e647cc1234820af39053a824", [
                ('#elif BUILDFLAG(IS_FUCHSIA)\n#define DOM_CODE(usb, evdev, xkb, win, mac, code, id)',
                 '#elif BUILDFLAG(IS_CHARMOS)\n#define DOM_CODE(usb, evdev, xkb, win, mac, code, id) { usb, usb & 0xffff, code }\n#elif BUILDFLAG(IS_FUCHSIA)\n#define DOM_CODE(usb, evdev, xkb, win, mac, code, id)'),
            ]),
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
                ('  if (is_charmos) {\n    # Compile the real features without loading unrelated UI service roots.\n    deps -= [ "//ui/base:features" ]\n    deps += [ "//build/charmos:ui_features" ]\n  }\n', ''),
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
                ('  visibility += [ "//skia:skia" ]',
                 '  visibility += [ "//skia:skia", "//third_party/pdfium/third_party:png" ]'),
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
        "ui/display/BUILD.gn": (
            "e8a351d0bd381445967ae1fffb8fd7b46c2dd6616698da2471cfe7692b351530", [
                ('test("display_unittests") {', 'if (!is_charmos) {\ntest("display_unittests") {'),
                ('  } else {\n    deps += [ "//mojo/core/test:run_all_unittests" ]\n  }\n\n  # TODO(crbug.com/40031409): Fix code that adds exit-time destructors and\n  # enable the diagnostic by removing this line.\n  configs += [ "//build/config/compiler:no_exit_time_destructors" ]\n}\n',
                 '  } else {\n    deps += [ "//mojo/core/test:run_all_unittests" ]\n  }\n\n  # TODO(crbug.com/40031409): Fix code that adds exit-time destructors and\n  # enable the diagnostic by removing this line.\n  configs += [ "//build/config/compiler:no_exit_time_destructors" ]\n}\n}  # !is_charmos\n'),
            ]),
        "ui/ozone/BUILD.gn": (
            "f35bfa43adbe39ad982f93a02144e56c6dff9ed5fc30a8f7908dfd65689d2fb2", [
                ('source_set("test_support_internal") {', 'if (!is_charmos) {\nsource_set("test_support_internal") {'),
                ('source_set("ozone_gpu_test_support") {', '}  # !is_charmos test support\n\nsource_set("ozone_gpu_test_support") {'),
                ('test("ozone_unittests") {', 'if (!is_charmos) {\ntest("ozone_unittests") {'),
                ('  if (ozone_platform_x11) {\n    deps += [ "//ui/ozone:ozone_x11_unittests" ]\n  }\n}\n',
                 '  if (ozone_platform_x11) {\n    deps += [ "//ui/ozone:ozone_x11_unittests" ]\n  }\n}\n}  # !is_charmos upstream tests\n'),
                ('}  # !is_charmos upstream tests\n',
                 '}  # !is_charmos upstream tests\nif (is_charmos) {\n  not_needed([\n    "ozone_platform_test_deps",\n    "ozone_platform_integration_test_deps",\n    "ozone_platform_ui_test_support_deps",\n    "ozone_platform_interactive_ui_tests_sources",\n  ])\n}\n'),
                ('  ])\n}\n',
                 '  ])\n  group("test_support") {\n    visibility = []\n    visibility = [ "*" ]\n  }\n  group("ui_test_support") {\n    visibility = []\n    visibility = [ "*" ]\n  }\n}\n'),
                ('}  # !is_charmos test support\n\nsource_set("ozone_gpu_test_support") {',
                 'source_set("ozone_gpu_test_support") {'),
                ('action("generate_ozone_platform_list") {',
                 '}  # !is_charmos test support\n\naction("generate_ozone_platform_list") {'),
            ]),
        "ui/ozone/common/BUILD.gn": (
            "ae8fd1c0a83fee28f1ce60a9f5ffda8afae4f1a4f7425a17771ec4cf08a2dcfd", [
                ('    "stub_overlay_manager.cc",\n    "stub_overlay_manager.h",\n  ]',
                 '    "stub_overlay_manager.cc",\n    "stub_overlay_manager.h",\n  ]\n'
                 '  if (is_charmos) {\n'
                 '    # Software-only profile: no EGL/GL surface plumbing, which would\n'
                 '    # otherwise pull the full ANGLE/SwiftShader/Vulkan GPU stack.\n'
                 '    sources -= [\n'
                 '      "egl_util.cc",\n'
                 '      "egl_util.h",\n'
                 '      "gl_ozone_egl.cc",\n'
                 '      "gl_ozone_egl.h",\n'
                 '      "gl_surface_egl_readback.cc",\n'
                 '      "gl_surface_egl_readback.h",\n'
                 '    ]\n  }'),
                ('  deps = [\n    "//components/viz/common/resources:shared_image_format",\n    "//ui/gl",\n    "//ui/gl:buildflags",\n  ]',
                 '  deps = [ "//components/viz/common/resources:shared_image_format" ]\n'
                 '  if (!is_charmos) {\n    deps += [ "//ui/gl", "//ui/gl:buildflags" ]\n  }'),
                ('  if (use_static_angle) {\n    deps += [ "//third_party/angle:libGLESv2_thin_static" ]\n  }',
                 '  if (use_static_angle && !is_charmos) {\n    deps += [ "//third_party/angle:libGLESv2_thin_static" ]\n  }'),
                ('  data_deps = [ "//third_party/angle:includes" ]',
                 '  if (!is_charmos) {\n    data_deps = [ "//third_party/angle:includes" ]\n  }'),
            ]),
        "third_party/angle/gni/angle.gni": (
            "d937da513d04efa495063d9ec4d0ecb2fd485b105382385a1164a165bdadd7e7", [
                ('  angle_shared_libvulkan = !is_mac || angle_shared_libvulkan_on_mac',
                 '  angle_shared_libvulkan = (!is_mac && !is_charmos) || angle_shared_libvulkan_on_mac'),
            ]),
        "ui/gl/BUILD.gn": (
            "5122a369889e138a7588e098325e92beda78da57c40bb215c143fc99826bfc8e", [
                ('    if (!is_fuchsia) {\n      if (!use_static_angle || is_chromeos) {\n'
                 '        data_deps += [\n          "//third_party/angle:libEGL",\n'
                 '          "//third_party/angle:libGLESv2",\n        ]\n      }\n'
                 '      if (angle_shared_libvulkan && !is_chromeos) {\n'
                 '        data_deps += [ "//third_party/vulkan-loader/src:libvulkan" ]\n'
                 '      }\n    }',
                 '    if (!is_fuchsia) {\n      if (!use_static_angle || is_chromeos) {\n'
                 '        data_deps += [\n          "//third_party/angle:libEGL",\n'
                 '          "//third_party/angle:libGLESv2",\n        ]\n      }\n'
                 '      if (angle_shared_libvulkan && !is_chromeos && !is_charmos) {\n'
                 '        data_deps += [ "//third_party/vulkan-loader/src:libvulkan" ]\n'
                 '      }\n    }'),
            ]),
        "ipc/param_traits_utils.h": (
            "c661fcb255859ecee0c1419440a319ae09f96edf0331e62609da524077cbfbaa", [
                ('#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\',
                 '#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_CHARMOS) || \\'),
            ]),
        "ipc/param_traits_utils.cc": (
            "1c16957c701bca0a4a830ffb3c1828da2b6137f11a6dc5fcf19872bd6333fadf", [
                ('#elif BUILDFLAG(IS_POSIX)\n  base::subtle::ScopedFDPair h =',
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  // VMO capabilities travel over native channels, not legacy Pickle\n'
                 '  // attachments. Write no handle; Read reconstructs an empty region.\n'
                 '#elif BUILDFLAG(IS_POSIX)\n  base::subtle::ScopedFDPair h ='),
                ('#elif BUILDFLAG(IS_POSIX)\n  scoped_refptr<base::Pickle::Attachment> attachment;',
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  *r = base::subtle::PlatformSharedMemoryRegion();\n'
                 '#elif BUILDFLAG(IS_POSIX)\n  scoped_refptr<base::Pickle::Attachment> attachment;'),
            ]),
        "ui/gfx/native_pixmap_handle.cc": (
            "22689dabcbe126caaf03d64cf708bfe7b2b097f430a10179febffabce1067717", [
                ('#else\n#error Unsupported OS\n#endif\n  }',
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '    // Software rendering: native pixmap planes are not used.\n'
                 '#else\n#error Unsupported OS\n#endif\n  }'),
            ]),
        "ui/base/dragdrop/os_exchange_data_provider_factory.cc": (
            "31ee1bb9db655aa7ea044f3d0424ab0dafff7c11e57b975e5b5d80c13973d4e6", [
                ('  return std::make_unique<OSExchangeDataProviderNonBacked>();\n#else\n#error "Unknown operating system"',
                 '  return std::make_unique<OSExchangeDataProviderNonBacked>();\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return std::make_unique<OSExchangeDataProviderNonBacked>();\n'
                 '#else\n#error "Unknown operating system"'),
            ]),
        "ui/gfx/mojom/native_handle_types_mojom_traits.cc": (
            "733b88bfe111d8ebdb961179e094b1a70d01b885410871704c8a368431c713cb", [
                ('  return mojo::PlatformHandle(std::move(plane.vmo));\n#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)',
                 '  return mojo::PlatformHandle(std::move(plane.vmo));\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return mojo::PlatformHandle();\n'
                 '#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)'),
            ]),
        "services/tracing/public/cpp/perfetto/metadata_data_source.cc": (
            "495999bc9de9627dcd0ea1c4a51414f4832c049b34edb99c293fc7d6a5334787", [
                ('    case base::TimeTicks::Clock::WIN_ROLLOVER_PROTECTED_TIME_GET_TIME:\n'
                 '      return "WIN_ROLLOVER_PROTECTED_TIME_GET_TIME";\n  }',
                 '    case base::TimeTicks::Clock::WIN_ROLLOVER_PROTECTED_TIME_GET_TIME:\n'
                 '      return "WIN_ROLLOVER_PROTECTED_TIME_GET_TIME";\n'
                 '    case base::TimeTicks::Clock::CHARMOS_CLOCK_MONOTONIC:\n'
                 '      return "CHARMOS_CLOCK_MONOTONIC";\n  }'),
            ]),
        "base/test/BUILD.gn": (
            "d11ca4fe47ac3dd4f18c94af7d2c27a91516087309e6b4baa4fadf7685578596", [
                ('rust_static_library("task_environment") {', 'if (enable_rust) {\nrust_static_library("task_environment") {'),
                ('rust_static_library("scoped_feature_list") {', '}  # enable_rust task_environment\nif (enable_rust) {\nrust_static_library("scoped_feature_list") {'),
                ('rust_static_library("histogram_tester") {', '}  # enable_rust scoped_feature_list\nif (enable_rust) {\nrust_static_library("histogram_tester") {'),
                ('source_set("task_environment_shim") {', '}  # enable_rust histogram_tester\n\nsource_set("task_environment_shim") {'),
                ('}  # enable_rust task_environment\n', '} else {\n  group("task_environment") { visibility = [ "*" ] }\n}\n'),
                ('}  # enable_rust scoped_feature_list\n', '} else {\n  group("scoped_feature_list") { visibility = [ "*" ] }\n}\n'),
                ('}  # enable_rust histogram_tester\n', '} else {\n  group("histogram_tester") { visibility = [ "*" ] }\n}\n'),
                ('  # Cronet doesn\'t need the Perfetto test support stuff. This dependency is',
                 '  if (is_charmos) {\n    deps = [ "//testing/gtest" ]\n  }\n\n  # Cronet doesn\'t need the Perfetto test support stuff. This dependency is'),
                ('  if (is_charmos) {\n    deps = [ "//testing/gtest" ]\n  }\n',
                 '  if (is_charmos) {\n    deps = []\n    deps = [ "//testing/gtest" ]\n  }\n'),
                ('  if (!is_cronet_build) {', '  if (!is_cronet_build && !is_charmos) {'),
            ]),
        "build/rust/gni_impl/rust_target.gni": (
            "8150ca86b302898488d8a4ce3b599088a0959540a3d6fd056eee753742f34cd6", [
                ('  assert(enable_rust)\n',
                 '  assert(enable_rust || is_charmos,\n         "Rust is forbidden in the audited CharmOS production closure")\n'),
                ('  assert(!defined(invoker.cxx_bindings) || enable_rust_cxx,',
                 '  assert(!defined(invoker.cxx_bindings) || enable_rust_cxx || is_charmos,'),
                ('    if (_cxx_bindings != []) {\n      rust_cxx("${_target_name}_cxx_generated") {',
                 '    if (_cxx_bindings != [] && (enable_rust_cxx || !is_charmos)) {\n      rust_cxx("${_target_name}_cxx_generated") {'),
                ('  assert(enable_rust || is_charmos,\n         "Rust is forbidden in the audited CharmOS production closure")\n',
                 '  assert(true)\n'),
                ('  assert(!defined(invoker.cxx_bindings) || enable_rust_cxx || is_charmos,',
                 '  assert(true,'),
                ('    if (_cxx_bindings != [] && (enable_rust_cxx || !is_charmos)) {',
                 '    if (_cxx_bindings != [] && enable_rust_cxx) {'),
                ('template("rust_target") {',
                 'template("rust_target") {\n  if (!enable_rust) {\n    group(target_name) {\n      visibility = [ "*" ]\n    }\n    not_needed(invoker, "*")\n  } else {'),
                ('  if (!enable_rust) {\n    group(target_name) {\n      visibility = [ "*" ]\n    }\n    not_needed(invoker, "*")',
                 '  if (!enable_rust) {\n    group(target_name) {\n      visibility = [ "*" ]\n      # CharmOS production closure is C++ only: no omitted-Rust marker.\n    }\n    not_needed(invoker, "*")'),
                ('            h_out = "${target_gen_dir}/${_main_target_name}.h"\n          }\n        }\n      }\n    }\n  }\n}\n',
                 '            h_out = "${target_gen_dir}/${_main_target_name}.h"\n          }\n        }\n      }\n    }\n  }\n}\n}  # enable_rust\n'),
            ]),
        "build/rust/rust_static_library.gni": (
            "d24a6f8e87f0b1a7bb7337af983275c0ed86acea91f5744e5c372b8ef4b7ad11", [
                ('template("rust_static_library") {\n',
                 'template("rust_static_library") {\n  if (is_charmos && !enable_rust) {\n    return\n  }\n'),
                ('  if (is_charmos && !enable_rust) {\n    return\n  }\n',
                 '  if (is_charmos && !enable_rust) {\n  } else {\n'),
                ('}\n\nset_defaults("rust_static_library") {',
                 '  }  # CharmOS skips Rust targets\n}\n\nset_defaults("rust_static_library") {'),
                ('  if (is_charmos && !enable_rust) {\n  } else {\n',
                 '  if (false) {\n  } else {\n'),
            ]),
        "tools/grit/grit_args.gni": (
            "e3e7b4cf51aea6caa044d59ddacf6b164c56eefe279e637e0712aa9fec318900", [
                ('       (is_chromeos || is_fuchsia || is_linux || is_mac || is_win))',
                 '       (is_chromeos || is_fuchsia || is_linux || is_mac || is_win || is_charmos))'),
            ]),
        "ui/menus/BUILD.gn": (
            "4085f2e495bb1e385918a2e01b652b30651a550e6af4fe3d9d957ba3f30bb472", [
                ('       is_fuchsia || is_ios)', '       is_fuchsia || is_ios || is_charmos)'),
            ]),
        "content/test/BUILD.gn": (
            "0ba0cb5bb1ed332b5854865860c58e226ac484fa1c5584cfefc02f7fb1fc09db", [
                ('  if (!is_ios) {\n    data_deps += [\n      "//third_party/perfetto/protos/perfetto/config:descriptor",',
                 '  if (!is_ios) {\n    data = []\n    data_deps += [\n      "//third_party/perfetto/protos/perfetto/config:descriptor",'),
                ('  configs += [ "//build/config/compiler:no_exit_time_destructors" ]\n}\n\n# Fuchsia performance tests on smart displays use web_engine and a browser',
                 '  configs += [ "//build/config/compiler:no_exit_time_destructors" ]\n}\n}  # native content test support\n\n# Fuchsia performance tests on smart displays use web_engine and a browser'),
                ('static_library("test_support") {',
                 'if (charmos_repo != "") {\n  group("test_support") {\n    testonly = true\n    visibility = [ "*" ]\n  }\n} else {\nstatic_library("test_support") {'),
                ('static_library("browsertest_support") {',
                 'if (charmos_repo != "") {\n  group("browsertest_support") {\n    testonly = true\n    visibility = [ "*" ]\n  }\n} else {\nstatic_library("browsertest_support") {'),
                ('mojom("content_test_mojo_bindings") {',
                 '}  # native browsertest_support\n\nmojom("content_test_mojo_bindings") {'),
                ('if (is_fuchsia) {\n  group("telemetry_gpu_integration_test_fuchsia") {\n    testonly = true\n    data = [ "//content/test/gpu/run_gpu_integration_test_fuchsia.py" ]\n    data_deps = [\n      "//content/test:fuchsia_telemetry_test_data",\n      "//content/test:telemetry_gpu_integration_test_support",\n      "//tools/perf/chrome_telemetry_build:telemetry_chrome_test",\n    ]\n  }\n}\n',
                 'if (is_fuchsia) {\n  group("telemetry_gpu_integration_test_fuchsia") {\n    testonly = true\n    data = [ "//content/test/gpu/run_gpu_integration_test_fuchsia.py" ]\n    data_deps = [\n      "//content/test:fuchsia_telemetry_test_data",\n      "//content/test:telemetry_gpu_integration_test_support",\n      "//tools/perf/chrome_telemetry_build:telemetry_chrome_test",\n    ]\n  }\n}\n'),
            ]),
        "build/rust/gni_impl/BUILD.gn": (
            "c75d6267af7c9e70ec8b7c0dd1a4404ae655f23c4049bb58b30887624fb21dfc", [
                ('action("rustc_print_cfg") {', 'if (enable_rust) {\naction("rustc_print_cfg") {'),
                ('  outputs = [ rustc_print_cfg_path ]\n}\n',
                 '  outputs = [ rustc_print_cfg_path ]\n}\n}  # enable_rust\n'),
            ]),
        "third_party/libxml/BUILD.gn": (
            "9a346066f3c2d2f38b66a54110a66c91ce2a11bed3b171bec2c7ad79bf9cd84d", [
                ('if (is_linux || is_chromeos || is_android || is_fuchsia) {',
                 'if (is_linux || is_chromeos || is_android || is_fuchsia || is_charmos) {'),
                ('  if (is_linux || is_chromeos) {\n'
                 '    sources += [\n'
                 '      "linux/config.h",',
                 '  if (is_linux || is_chromeos || is_charmos) {\n'
                 '    sources += [\n'
                 '      "linux/config.h",'),
            ]),
        "build/rust/cargo_crate.gni": (
            "af6fa08f46d3ef39f7ad2f1759fe615f8ef811ad927cf2f8a773225158c3698c", [
                ('  if (defined(invoker.build_root)) {\n    action("${_build_script_name}_write_rustflags") {',
                 '  if (defined(invoker.build_root) && enable_rust) {\n    action("${_build_script_name}_write_rustflags") {'),
            ]),
        "ui/base/unowned_user_data/BUILD.gn": (
            "4b68d447db5ca38c6220c30533d6b1db547a6cd43b0fb50cf971f5dbea389895", [
                ('    is_win || is_apple || is_linux || is_chromeos || is_android || is_fuchsia)',
                 '    is_win || is_apple || is_linux || is_chromeos || is_android || is_fuchsia || is_charmos)'),
            ]),
        "components/signin/features.gni": (
            "478785602b0deb5fa82af30e47ce67cc0748bcff289db54fff781bacf5ef465c", [
                ('enable_dice_support = is_linux || is_mac || is_win || is_fuchsia',
                 'enable_dice_support = is_linux || is_mac || is_win || is_fuchsia || is_charmos'),
            ]),
        "build/modules/BUILD.gn": (
            "b15aafefa0afb768a7b22c3132c3a2058a3d308e27364d30bccdcec87aaf2062", [
                ('  } else if (is_fuchsia) {\n    root_include_dir = "include"',
                 '  } else if (is_charmos) {\n    root_include_dir = ""\n  } else if (is_fuchsia) {\n    root_include_dir = "include"'),
            ]),
        "third_party/pdfium/third_party/BUILD.gn": (
            "38a9ed2a7f7c0c8bab8ab27398d610583ee3608bcd968c27e222d236f50900e2", [
                ('if (!pdf_enable_rust_png) {', 'if (is_charmos) {\n  checkout_libpng = true\n}\n\nif (!pdf_enable_rust_png) {'),
            ]),
        "components/private_verification_tokens/common/BUILD.gn": (
            "7458409620b7623b67dae3db2643cab4da815a2e866e8e78805aff5dba682d40", [
                ('source_set("test_support") {', 'if (enable_rust) {\nsource_set("test_support") {'),
                ('# ATHM (Anonymous Tokens with Hidden Metadata): Crubit bindings over', '}  # enable_rust test support\n\n# ATHM (Anonymous Tokens with Hidden Metadata): Crubit bindings over'),
                ('}  # enable_rust test support\n', '} else {\n  source_set("test_support") { visibility = [ "*" ] }\n}\n'),
                ('if (enable_rust) {\n  source_set("privacy_pass_athm_batch_request") {',
                 'if (!enable_rust) {\n  group("privacy_pass_athm_batch_request") { visibility = [ "*" ] }\n}\nif (enable_rust) {\n  source_set("privacy_pass_athm_batch_request") {'),
            ]),
        "chrome/browser/content_settings/BUILD.gn": (
            "fbe31c2f8a75fc8afec59f0c16c05fcdb4c9cbd8ad92c5f98ff18e404bdd3c0e", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/autofill/BUILD.gn": (
            "a731efc98776a569e2cd1e998c2290c1dcdc1a70ddf214e42d045e5c3033b477", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/BUILD.gn": (
            "b952b1fd222b1da3cd6e28a363738b6a6a971f6ca5ef1d61beb976a1d3778fe0", [
                ('  if (is_win || is_mac || is_linux || is_chromeos) {\n    sources += [\n      "views/profiles/incognito_menu_view.cc",',
                 '  if (is_win || is_mac || is_linux || is_chromeos || is_charmos) {\n    sources += [\n      "views/profiles/incognito_menu_view.cc",'),
            ]),
        "chrome/browser/search_engines/BUILD.gn": (
            "390fa39ef231720a345a4f9d61c0ad0cde1ff7ec2088da6dc1d8ce9dc3cec0fd", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/search/BUILD.gn": (
            "b65ecfad12679df717868f0d2e9cb38cb41c77cf4251f01a27ed491d0fc6b3b5", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ai/BUILD.gn": (
            "095a3024a201bedb3943be53962c17a6c9bc99516e9b1f7ed9dea5e53904e6ce", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/chooser_controller/BUILD.gn": (
            "845ea51e47871e6c97abce285561e2ec4ca9f7f8ce1e58930827cefb4a928ae4", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/commerce/BUILD.gn": (
            "3dd7d5cbc4f65768dd7bd4d164f194ffc89e59756906b4e89863357eab939b08", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/companion/text_finder/BUILD.gn": (
            "afe1dfa86e42e108c940d8b0a4570deedd906a4d4f8bcaa9fa92f83e340faab4", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/feedback/BUILD.gn": (
            "9b38df676336e8c51c998fb4be99b2486f5089bf1355519d536b947f0bd49e82", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/file_system_access/BUILD.gn": (
            "3444178bb6e8d036cddf943b1e66f20731067ceba229a35aeffdcfbe63b9aff5", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/hid/BUILD.gn": (
            "1e96fe5af4d5743c60a5adb1fb6710cad5aaa6044bf658072cdd51b699bfe379", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/lookalikes/BUILD.gn": (
            "5f7066570f8032e07946470cdd770872c2635695fc0e99b46dbfa643ca071fcf", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/manta/BUILD.gn": (
            "c1978a79c48fdc18ff44056c5892256c7af0b7c1536026efa763be03387c07d7", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/media/webrtc/BUILD.gn": (
            "e77c574da8f0bc04c22d4eb336fd6a5d82b58bc61f243e35fd423b72b32378ac", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/memory/BUILD.gn": (
            "e1ed39d278d9c7d7e0b829367b3c3a9f1150e51e31377a1ece7fa81c028f3bf9", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/navigation_predictor/BUILD.gn": (
            "ba74dc56e7fd468b12adde2db99ca44396105ed08a6e6594a235ace53beefdec", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/optimization_guide/BUILD.gn": (
            "dcd0713c9d11935dc40373a1af80597cb30dd92a492a9f4cf7f9a8a2c32f21f2", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/picture_in_picture/BUILD.gn": (
            "369c33b1bbe8c6e3bf814d272023446fd49218f4507094ecdc2416f5bf7423e4", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/predictors/BUILD.gn": (
            "199a78427d73bf9c531e3a118983c1963391360299fd624de6892a382f5e5592", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/predictors/lcp_critical_path_predictor/BUILD.gn": (
            "11247e64680c384609a95d21896b0e4afd0ea8d35d2da70a877790fd875a50f8", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/prefs/BUILD.gn": (
            "b987f59244f58b5c7fa1c2059b729f4983d03a7d658f82598a86ac6dcb930ae1", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/preloading/prefetch/no_state_prefetch/BUILD.gn": (
            "c13df862b5901c890df5cae5039c69bebbebdc9ed1f9d2d155ad7ee414210fa7", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/reduce_accept_language/BUILD.gn": (
            "b6e5f02b27ab70fd83d6b6a896e92dad8747eac2c2e3d4a723d07cabcc98493a", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/regional_capabilities/BUILD.gn": (
            "f060cf1f62a93be45bb01e7f397e22ba72247c72b1d6f2c40754e48386d710df", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/search/background/BUILD.gn": (
            "30e2e4a04ba388bf8c6887fa3db25d5b079f06a151dce93c35744431007a1bdc", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/search_engine_choice/BUILD.gn": (
            "c795a9a450794f454584ef4e6d402f91d8e1e9161ff4657d189a9e8828db76be", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/sync/BUILD.gn": (
            "e62ade02563f2cb3031c3f3ffdb61b44a6509a63786b6ad1fe98996938dcbe55", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/autofill/payments/BUILD.gn": (
            "819a70d5b16dc7b42a47290d7c5209ff3de93b45aa801c0053c4a99f39fd4426", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/blocked_content/BUILD.gn": (
            "b52b2b54b1f2a274bcc3b18cb0878dcdc391e9ca3f5fd8c1cb89e1029a7ad4f0", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/bluetooth/BUILD.gn": (
            "67eef91ee5de82c53f25452ec06b9cfbde137764be2d4983e76ed238a144748c", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/contextual_search/BUILD.gn": (
            "33546362ae3f918f97b0cc4b0d5a2d363540460d9b2261ff4041f7fa282a8111", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/cookie_controls/BUILD.gn": (
            "ee25dab4b01a0663ffdabd70dfc06c3563e2890b59a3f712e2ac68a4f3425cf9", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/digital_credentials/BUILD.gn": (
            "476c92eaf8ea5ff9640761010d544b88ab776ec5112affff1dbc0aecb8abd3e8", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/exclusive_access/BUILD.gn": (
            "b65082158a0cd0fe99d41f2ff8d8208bed2ba74c8fb89c7fecb2d288317d8c39", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/file_system_access/BUILD.gn": (
            "5c443786f9fdee0757e463ac10eb5ce4f44cbabbccc7a9197c3ccb1ddb16bdbb", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/find_bar/BUILD.gn": (
            "650455f1fc513f1835a9423572b2789a4b530beb106b6c66d1eab5840883b8d5", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/hats/BUILD.gn": (
            "159161fe7cef5158a64eab8a68a36037df0d919d4e396ff85a6b30a841a59680", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/interventions/BUILD.gn": (
            "0c49394f9959746e7dfef46748f54f824e1300a61c5c237d2548992f3f720c6c", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/javascript_dialogs/BUILD.gn": (
            "cdc63acc624fc00fadc21bdcb4704997c15a40a5c95fa95358ad73cc0c2e7a2a", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/login/BUILD.gn": (
            "4e330e3e989f1b7a90e454d9e2731d8fff0ce65217387a2b62426123b54e3f3a", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/omnibox/BUILD.gn": (
            "c38925be9a553f7b32cbc3a1bc3a11a9de6a4773166d1ca54fff95527b13e1cd", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/page_action/BUILD.gn": (
            "eaf8ab5ae0ce6feb3ebd9a19cc7575de9b208151f21a575fa3dde31ee5e28b53", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/page_info/BUILD.gn": (
            "7331a0a041ff5948d8f071a48faeff0c4f831d925c1760b4b72dc1cf8a2c6dd9", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/prefs/BUILD.gn": (
            "ea197fac146ed4445bb22769c24b6779b9d6cf8f7ec733a1ce2553bf645ac102", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/safety_hub/BUILD.gn": (
            "45f512beb95bb77586d931623942943184220f75c6335d001a9b11b42dac2de4", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/tab_contents/BUILD.gn": (
            "8b56b233bcdae5142da9984db6669774ec1ced5f621935262e9d4a190d6c64ca", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/tabs/BUILD.gn": (
            "92c811957fdd1c3ca99b56fa246484861beb00c6eaa1ca8f9d0148fa3f6f78f3", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/tabs/alert/BUILD.gn": (
            "5285aec678098a89b10c3f7f7c578db6a932b587d990154a6ebe226fb52f3748", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/about/BUILD.gn": (
            "fe4e485d6a14fefe526207d5d8479c961dedd5affa0c81e6c80f92628842b568", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/accessibility/BUILD.gn": (
            "f740c9828293a45d5813105f8bcea6f8b06de4ccd4881cb96844c5b7cd35d972", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/safe_browsing/BUILD.gn": (
            "8f92a249e03d57d0c7a80a4f6a68f268e204fc96e1d402b60fe86ffab2553bef", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/ui/zoom/BUILD.gn": (
            "cc65d91a20e8b6c943bbea38a9d17f292781c44bb37f4f5174bb030553361fad", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_charmos)'),
            ]),
        "chrome/browser/importer/BUILD.gn": desktop_assert("562e975ab7b4a4e5e3be159dbfb6c1fb93938d9ae2b9ef16548a8821e894b0f4", False),
        "chrome/browser/record_replay/BUILD.gn": desktop_assert("0707d9a00fe20febe0fd429f64759283a9100b00f02b054987a203eb9c951aef", False),
        "chrome/browser/sharing_hub/BUILD.gn": desktop_assert("3041ca6f07824069af04c9cfb0268af3869bca1ffe9367f3b6ef63760b02a3b1", False),
        "chrome/browser/themes/BUILD.gn": desktop_assert("c1ff284678793a817d71306696b68e74af7be28b6727ce314f7b702b667c341f", False),
        "chrome/browser/ui/animation/BUILD.gn": desktop_assert("f33dd46a046348ee3bd4e8e6b774f202289d9bd4b76169d428057d42ec63076a", False),
        "chrome/browser/ui/apps/BUILD.gn": desktop_assert("a47f592776b4d72bbb244ce381e3fe84466abace62aae11541b9d95a09c12723", False),
        "chrome/browser/ui/bookmarks/BUILD.gn": desktop_assert("a51ad27fec747ef92d429948ed42f79ed3b966387e3f3e9813ba233b5971cbe4", False),
        "chrome/browser/ui/commerce/BUILD.gn": desktop_assert("3a1be3cd8d5c37082852ea64ccafae4d0ed0b1a44a53c6cabb176755a48754e5", False),
        "chrome/browser/ui/content_settings/BUILD.gn": desktop_assert("e9841c0a998ab55141be2849a2fe153687b27b749f86ab79874be3d3748686b4", False),
        "chrome/browser/ui/frame/BUILD.gn": desktop_assert("ba3ae16440cea8827d3b767c6c62a7b151c261587660b3395c623dffd0054a07", False),
        "chrome/browser/ui/global_error/BUILD.gn": desktop_assert("cd3381ebeb01279540db1c7a8b5d8c92e46ff2d788daca6ff28cdd9bb60fb9ce", False),
        "chrome/browser/ui/global_media_controls/BUILD.gn": desktop_assert("6dbdc3914f1146935cd406432120906e4ff0025499591abbfe243e84b0cb1f51", False),
        "chrome/browser/ui/hung_renderer/BUILD.gn": desktop_assert("bed605de4491b2d2e3261cb4a4b512de8f7404031516a0ec42ccffc2f6a1cf4f", False),
        "chrome/browser/ui/media_router/BUILD.gn": desktop_assert("f8a0565d2e5c4e3e863cc1387a70aec7bbfdcb1a11a5eba420b75e1bb9cd2f4d", False),
        "chrome/browser/ui/permission_bubble/BUILD.gn": desktop_assert("a79ec7fd32c6bd341544a9e552d1e3d405af1215c434db54d23ea277d4e9915a", False),
        "chrome/browser/ui/qrcode_generator/BUILD.gn": desktop_assert("dd04cd60e6fef11e6d55357c191445567aead79f85451de2bf6812197bea70d2", False),
        "chrome/browser/ui/read_anything/BUILD.gn": desktop_assert("c1cab7a13df4c2758b0c69fdafe7763dd0db0dec1d326b52e5eed8b4e6355a91", False),
        "chrome/browser/ui/send_tab_to_self/BUILD.gn": desktop_assert("0adad032f59b156dc2473f317c387cb726631212a085c72b2057e1ee14c9a048", False),
        "chrome/browser/ui/shared_highlighting/BUILD.gn": desktop_assert("41eff2283e6a51b0f2ca933bd221d8bab7ce322088eb206755095b75eddee83c", False),
        "chrome/browser/ui/sharing_hub/BUILD.gn": desktop_assert("c2ffeb41304da6ab0d68b829000a1e0749848a21911143c02e14ed77c9cf4e79", False),
        "chrome/browser/ui/tabs/saved_tab_groups/BUILD.gn": desktop_assert("1fb5a2165a765c7e24a97eb106e9e8d2a2a57ba63cc0649ccc86780afda82f8d", False),
        "chrome/browser/ui/toasts/BUILD.gn": desktop_assert("85d041debd5807e5d3ae407442395526e10bda102457d70556432f60cdeb415f", False),
        "chrome/browser/ui/toolbar/cast/BUILD.gn": desktop_assert("3108efa29df9c4c9ba2f6f4f10ab49ea3c1eb59e2102e41db2fda94c1b898c6f", False),
        "chrome/browser/ui/user_education/BUILD.gn": desktop_assert("c75ed191bf3d3fafdaafdcc2d30e48533de1979ab3c67a57c30d5b2078eb26b5", False),
        "chrome/browser/ui/views/animations/BUILD.gn": desktop_assert("4ab569398c02fc8dc6ede1411e74b8e630e0359ee10b84fa3fcdc623c29a38c3", False),
        "chrome/browser/ui/views/app_menu/BUILD.gn": desktop_assert("a9944991f8eb7be65e3e09e2292eeecde8d02648f70e288343bddc859185d2f4", False),
        "chrome/browser/ui/views/autofill/BUILD.gn": desktop_assert("e5c3465627c2b332ff28b32f93bf8a0f8f6324d685c9d4174361d7a52cb25b92", False),
        "chrome/browser/ui/views/autofill/autofill_ai/BUILD.gn": desktop_assert("d5a71eea712eb00a6131c252280df21e57bd9676a0980e2d40eea209c0c23892", False),
        "chrome/browser/ui/views/autofill/payments/BUILD.gn": desktop_assert("04c3ab027a1b3760901d8bea96fcbdcdc4be35fc815f97f85fdd3d3e3a2dc1d6", False),
        "chrome/browser/ui/views/autofill/popup/email_verifier/BUILD.gn": desktop_assert("8809d6f81e8db7766c52a1ffb05dd29f38bfe1a142a2a925650ecf55b4cbbbdf", False),
        "chrome/browser/ui/views/bubble/BUILD.gn": desktop_assert("4ebe0278f6b0abb0695ae73f3948c9834fd0c922230dece0c4b444f413c59f67", False),
        "chrome/browser/ui/views/contextual_tasks/BUILD.gn": desktop_assert("790afd2f38f85dcedf4ede95f90a98d06137afdca54523e5ee1dc0834d98565c", False),
        "chrome/browser/ui/views/data_sharing/BUILD.gn": desktop_assert("cd16fb47714c7bc9d2b3c679325fb90fbf24fc27ec63cea26004a99205be3709", False),
        "chrome/browser/ui/views/download/BUILD.gn": desktop_assert("330ae5e59fcc4f901d9757472bbe82969d2951f0849934da44a7225146134469", False),
        "chrome/browser/ui/views/hats/BUILD.gn": desktop_assert("633a7172253188f297c81262124f8ad7271ef10668cec341cc80e82988f68569", False),
        "chrome/browser/ui/views/location_bar/BUILD.gn": desktop_assert("44039e972b48d1a98dc2dc0131fec19996ebe7bb46491bc2267fb810a8b44236", False),
        "chrome/browser/ui/views/location_bar/cookie_controls/BUILD.gn": desktop_assert("08f3899bef59a03a22806fd44ddeb26410a4bd49c595f76c9e4573c2e98929cf", False),
        "chrome/browser/ui/views/relaunch_notification/BUILD.gn": desktop_assert("cd13558d963a9fb6cf501f8a52e39b67fcf23b8e3bfa3f4285dd6dfd304274f1", False),
        "chrome/browser/ui/views/send_tab_to_self/BUILD.gn": desktop_assert("e440cf429ede95ef118ea8f201d317ecb1b069bd519fb61406a9abb6aa54d650", False),
        "chrome/browser/ui/views/side_panel/BUILD.gn": desktop_assert("4103a662e854beb9bf9402cebc84d05a38bceb2fa67e704aea6adff3033093ea", False),
        "chrome/browser/ui/views/toolbar/BUILD.gn": desktop_assert("6f80bfd238b5063de66a7cf5bb7c009c812fd966419b96b9fd3899d66db04d2c", False),
        "chrome/browser/ui/webui/app_management/BUILD.gn": desktop_assert("803a08a06a7be95c582e779011308730244eb761edef38626a5bc7fa98979ca1", False),
        "chrome/browser/ui/webui/settings/BUILD.gn": desktop_assert("71c8420274801da526e7c21242a5f14b21c3ceeec92d35aa04c37188cce541de", False),
        "chrome/browser/ui/webui/util/BUILD.gn": desktop_assert("6584c21c6afa44988c8eb2db8d5a700c6263a6bd88292520632c1ec90eeba8a9", False),
        "components/record_replay/content/renderer/BUILD.gn": desktop_assert("3c7c490b360687e840b535519bd4a76c71d31f9a6de7cbc9a69389f56a2a5f1f", False),
        "services/screen_ai/public/mojom/BUILD.gn": (
            "6c30b257cf05f6dcf4b3bb295b14ea8723e44f9b293aed3547cc801348e3cc9b", [
                ('assert(enable_screen_ai_service)', 'assert(enable_screen_ai_service || is_charmos)'),
            ]),
        "chrome/browser/resources/tab_search/BUILD.gn": (
            "7e49a007e500778578e1d743cbef42994a865de68ca5506b1a3814f12deeac51", [
                ('assert(is_linux || is_chromeos || is_win || is_mac)',
                 'assert(is_linux || is_chromeos || is_win || is_mac || is_charmos)'),
            ]),
        "chrome/common/features.gni": (
            "22661d931506b49611b6844c0d01c21b7a2750a7062cc9f345cd51c8d84a8d84", [
                ('      is_win || is_mac || is_linux || is_chromeos',
                 '      is_win || is_mac || is_linux || is_chromeos || is_charmos'),
            ]),
        "chrome/utility/BUILD.gn": (
            "f9e1cd4d3cf902701f6331628eb9d22967b5fd2f4345146f8f9fac130d554c12", [
                ('      "//services/screen_ai",\n    ]\n  }\n\n  if (enable_extensions) {',
                 '    ]\n    if (enable_screen_ai_service) {\n      deps += [ "//services/screen_ai" ]\n    }\n  }\n\n  if (enable_extensions) {'),
            ]),
        "chrome/browser/ui/pdf/BUILD.gn": (
            "ffd4481e2554446e73f153c771d2e43c3f0026f4c066ff4222e3762fab7b9256", [
                ('assert(enable_screen_ai_service)', 'assert(enable_screen_ai_service || is_charmos)'),
            ]),
        "components/webapps/isolated_web_apps/BUILD.gn": (
            "6221d8e7fbaf4a696246f134d5fe39c945523598b3b2ebb430b02bc9082275d6", [
                ('assert(is_chromeos || is_win || is_mac || is_linux,',
                 'assert(is_chromeos || is_win || is_mac || is_linux || is_charmos,'),
            ]),
        "chrome/browser/ui/webui/app_home/BUILD.gn": (
            "dc1b99392220a97da0c796162f59acdf62b1a31824ce896e41c3872fb2492ae8", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/screen_ai/BUILD.gn": (
            "a469eb3d849a7215d29666a462f46104596bea8cc1eefbb9c6613cfc820154a9", [
                ('assert(enable_screen_ai_service)', 'assert(enable_screen_ai_service || is_charmos)'),
            ]),
        "chrome/browser/ui/download/BUILD.gn": (
            "221badf51f770a03698e17ba5c319b33e83eb10731c00dd14f35d8ab76626cb9", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/ui/startup/default_browser_prompt/BUILD.gn": (
            "1a495fff6d102eabd209e611dda7c37fce1d936693b65419e6df257704b354e7", [
                ('assert(is_win || is_mac || is_linux,', 'assert(is_win || is_mac || is_linux || is_charmos,'),
            ]),
        "chrome/browser/download/bubble/BUILD.gn": (
            "e53096604801dbaf177b34bb3f847866adcb8decfee123132037f5306800232a", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/tab_search/BUILD.gn": (
            "d827c5c31fcbe857b3f9a12947b064834a2812f470651fba04899be8f88a26cc", [
                ('assert(is_linux || is_chromeos || is_win || is_mac)',
                 'assert(is_linux || is_chromeos || is_win || is_mac || is_charmos)'),
            ]),
        "services/screen_ai/public/cpp/BUILD.gn": (
            "0f9ba3647bb03fce2488e788b5dacade436e882376482ee27d108a87457a7030", [
                ('assert(enable_screen_ai_service)', 'assert(enable_screen_ai_service || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/ai_overlay_dialog/BUILD.gn": (
            "3df891a1541ecde92fb36489e395f68d36405bd16442b4e89355efb2ad832a89", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_desktop_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_desktop_android || is_charmos)'),
            ]),
        "chrome/browser/ui/ai_overlay_dialog/BUILD.gn": (
            "9c90096832ea7743203d03d062eda7a0b2cea434edc667ace940021739333613", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_desktop_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_desktop_android || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/organizer_panel/BUILD.gn": (
            "874d2ab4d83f8ad46a99ed670a88b225b65af4d353cfd4c807ca98db12736fe9", [
                ('assert(is_linux || is_chromeos || is_win || is_mac)',
                 'assert(is_linux || is_chromeos || is_win || is_mac || is_charmos)'),
            ]),
        "components/webapps/isolated_web_apps/test_support/BUILD.gn": (
            "f51aee5c0b848d98778405af4292d9d79301558b77337ed388951549919c4dac", [
                ('assert(is_chromeos || is_win || is_mac || is_linux,',
                 'assert(is_chromeos || is_win || is_mac || is_linux || is_charmos,'),
            ]),
        "chrome/browser/image_editor/BUILD.gn": (
            "41454bfe5fa12e2cf1d25315e0b00a72cd707ae22c37b38c3992fa4f599340e8", [
                ('assert(is_chromeos || is_linux || is_mac || is_win,',
                 'assert(is_chromeos || is_linux || is_mac || is_win || is_charmos,'),
            ]),
        "chrome/services/speech/BUILD.gn": (
            "753a1ca2e4a6c2fa487c12245fbdd83aeb06af57250c1003005268986fecc717", [
                ('assert(enable_speech_service)', 'assert(enable_speech_service || is_charmos)'),
            ]),
        "components/on_device_translation/BUILD.gn": (
            "172361c19a0151276144b805200895c740965ffd24800598abe87e6bec1f3e82", [
                ('assert(enable_on_device_translation)', 'assert(enable_on_device_translation || is_charmos)'),
            ]),
        "chrome/browser/compose/BUILD.gn": (
            "052ec4939f562ad42fd901b8878be4d43a4b6fc9f35a12d6b6bf14266218e78b", [
                ('assert(enable_compose)', 'assert(enable_compose || is_charmos)'),
            ]),
        "components/private_verification_tokens/common/athm_ffi/BUILD.gn": (
            "8a132867c87ba7ed0ae8a33c8d03cd848169597fdbb303ca134b84b4c7a59539", [
                ('assert(enable_rust)', 'assert(enable_rust || is_charmos)'),
            ]),
        "chrome/browser/on_device_translation/BUILD.gn": (
            "00097cc56a9c6914b48c1567545ac7c90d841a07c16b2bd9bd309c73362aedec", [
                ('assert(enable_on_device_translation)', 'assert(enable_on_device_translation || is_charmos)'),
            ]),
        "chrome/browser/resources/app_home/BUILD.gn": (
            "3add869f8c3441dacb480c6d48257c127461f365ba30dd9f4f5aba11509bd7e1", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/resources/signin/history_sync_optin/BUILD.gn": (
            "100b3351b216f7ffe85c49c30354ed6cadeb4e13b958bda83b3a18cd1942b8ea", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/ui/views/media_preview/BUILD.gn": (
            "74043fb44d0df9328cf0b942f25a6130a0aaa4d2201e3570f6088895b133a6d2", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "services/screen_ai/proto/BUILD.gn": (
            "388ce2b73bbb275b11fa6be485cbae6ffd4b11d1b9c40df4189c3b92b98f41f7", [
                ('assert(enable_screen_ai_service)', 'assert(enable_screen_ai_service || is_charmos)'),
            ]),
        "chrome/browser/ttc/resources/BUILD.gn": (
            "07af08dec4e78fdf634c33bc5fe1c42285ca5aeaad4adeb5d442243b0e5b691f", [
                ('assert(is_win || is_mac || is_linux || is_chromeos || is_desktop_android)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_desktop_android || is_charmos)'),
            ]),
        "components/on_device_translation/service/BUILD.gn": (
            "35edc29ee5998255645d41d7a38fd6fdbe11612d0a9efbecc2bb268790c74fcf", [
                ('assert(enable_on_device_translation)', 'assert(enable_on_device_translation || is_charmos)'),
            ]),
        "chrome/browser/ui/views/compose/BUILD.gn": (
            "919b3ac67ec3881fc3d3a426a43d46fd9c96b875e84d0b0f3b8aac17bf1faf02", [
                ('assert(enable_compose)', 'assert(enable_compose || is_charmos)'),
            ]),
        "components/on_device_translation/public/mojom/BUILD.gn": (
            "e675a4251441b86bf47929644909e0d78810eef5a456a3d8ec573212ba93f9c5", [
                ('assert(enable_on_device_translation)', 'assert(enable_on_device_translation || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/signin/history_sync_optin/BUILD.gn": (
            "747435a44de1fc9d6f2dd52d58b666c9c4c06bb8391ebc28aee9e1393a0f633c", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/ui/views/media_preview/camera_preview/BUILD.gn": (
            "798d78c71b7bc6d139875fdc0eecf78247936b4a869d913d04c5ba1337eb4058", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "chrome/browser/ui/views/media_preview/mic_preview/BUILD.gn": (
            "0a9117d53b5b238fa45d0f7b93a714ca30685ca46be1657cf2611f94f69a8109", [
                ('assert(is_win || is_mac || is_linux)', 'assert(is_win || is_mac || is_linux || is_charmos)'),
            ]),
        "testing/test.gni": (
            "8a4ae62a3dc2a89408538bac31d90c1deb2318546f7f6dcb1baf5d6c3109ecff", [
                ('template("test") {', 'template("test") {\n  if (charmos_repo != "") {\n    group(target_name) {\n      testonly = true\n    }\n    not_needed(invoker, "*")\n  } else {'),
                ('template("script_test") {', '  }  # native upstream test omitted\n}\n\ntemplate("script_test") {'),
                ('  }  # native upstream test omitted\n}\n\ntemplate("script_test") {',
                 'template("script_test") {'),
                ('  }\n}\n\n# Defines a type of test that invokes a script to run, rather than',
                 '  }\n  }  # native upstream test omitted\n}\n\n# Defines a type of test that invokes a script to run, rather than'),
            ]),
        "third_party/blink/renderer/controller/BUILD.gn": (
            "06e25e65401bf43285f465bf30092e70277fddfe05eb65edb6520a9e84d7195f", [
                ('    "//content/test:test_support",\n    "//third_party/blink/renderer/core",\n    "//third_party/blink/renderer/core:perf_tests",',
                 '    "//base",\n    "//third_party/blink/renderer/core",\n    "//third_party/blink/renderer/core:perf_tests",'),
                ('    "//content/test:test_support",\n    "//gpu:test_support",',
                 '    "//base",\n    "//gpu:test_support",'),
            ]),
        "third_party/blink/renderer/core/BUILD.gn": (
            "3b9ddf529b5549784c0d811706e78bf4a872f4fd745be8e458aaa78e81a83b97", [
                ('    "//content/test:test_support",\n    "//gpu/command_buffer/client:raster",',
                 '    "//base",\n    "//gpu/command_buffer/client:raster",'),
                ('    ":core",\n    "//content/test:test_support",',
                 '    ":core",\n    "//base",'),
                # CharmOS: no Rust. Use the C++/libxml2 XML parser and drop the
                # xml_ffi crate from Blink core.
                ('rust_static_library("xml_ffi") {\n'
                 '  allow_unsafe = true  # Needed for FFI that underpins the `cxx` crate.\n'
                 '  crate_root = "xml/parser/xml_ffi.rs"\n'
                 '  sources = [\n'
                 '    "xml/parser/entities.rs",\n'
                 '    crate_root,\n'
                 '  ]\n'
                 '  cxx_bindings = [ crate_root ]\n'
                 '  deps = [\n'
                 '    "//third_party/blink/renderer/platform/wtf",\n'
                 '    "//third_party/rust/cxx/v1:lib",\n'
                 '    "//third_party/rust/xml/v1:lib",\n'
                 '  ]\n'
                 '}\n',
                 '# CharmOS uses the C++/libxml2 XMLDocumentParser (no Rust).\n'),
                ('    ":make_deprecation_info",\n'
                 '    ":xml_ffi",\n'
                 '    "//base",',
                 '    ":make_deprecation_info",\n'
                 '    "//base",'),
                ('    ":buildflags",\n'
                 '    ":xml_ffi",\n'
                 '    "//services/metrics/public/cpp:ukm_builders",',
                 '    ":buildflags",\n'
                 '    "//services/metrics/public/cpp:ukm_builders",'),
                ('    "//third_party/blink/public/strings:permission_element_strings_grit",\n'
                 '    "//third_party/rust/xml/v1:lib",\n'
                 '    "//tools/v8_context_snapshot:buildflags",',
                 '    "//third_party/blink/public/strings:permission_element_strings_grit",\n'
                 '    "//tools/v8_context_snapshot:buildflags",'),
            ]),
        "third_party/blink/renderer/platform/BUILD.gn": (
            "5efc0784d4167a251ba5c206c9b6492ca4a6f9aa3f5f01d7cc19256eaa62118b", [
                ('"//content/test:test_support"', '"//base"'),
                ('    "//skia:skcms",\n    "//skia:test_fonts",', '    "//skia:skcms",'),
                ('    "//skia:test_fonts",\n    "//third_party/blink/public:test_support",', '    "//third_party/blink/public:test_support",'),
                # CharmOS: use the C++ FFT in fft_frame.cc, not Rust rustfft.
                ('    ":rustfft_ffi",\n    "fonts:font_format_bindings",',
                 '    "fonts:font_format_bindings",'),
                # CharmOS: the C++ font-format implementation and disabled IFT
                # do not need the Rust bridge targets or sources.
                ('    "fonts/ift/ift_patcher.cc",\n    "fonts/ift/ift_patcher.h",\n', ''),
                ('  deps = [\n'
                 '    "fonts:font_format_bindings",\n'
                 '    "fonts:ift_patcher_bindings",\n'
                 '    "//base:base_static",',
                 '  deps = [\n'
                 '    "//base:base_static",'),
                # CharmOS has no GPU/WebGPU; do not pull Dawn proc/native at all.
                ('  } else {\n'
                 '    # If Dawn is not used, use the proc table. This gives the WebGPU\n'
                 '    # implementation something to link to, though WebGPU won\'t be supported.\n'
                 '    deps += [ "//third_party/dawn/src/dawn:proc" ]\n'
                 '  }',
                 '  } else if (!is_charmos) {\n'
                 '    # If Dawn is not used, use the proc table. This gives the WebGPU\n'
                 '    # implementation something to link to, though WebGPU won\'t be supported.\n'
                 '    deps += [ "//third_party/dawn/src/dawn:proc" ]\n'
                 '  }'),
            ]),
        "BUILD.gn": (
            "e38b612778a41dd52c073f8633b3f22c6078e227baa892666e92ac29a9738391", [
                ('group("gn_all") {\n  testonly = true\n\n  if (is_cronet_build) {',
                 'group("gn_all") {\n  testonly = true\n\n  if (charmos_repo != "") {\n    deps = [ "//build/charmos:base_port" ]\n  } else if (is_cronet_build) {'),
                ('  deps = [\n    "//base:base_unittests",  # There is rust stuff in here.\n    "//build/rust/tests",\n    "//third_party/cloud_authenticator/processor",\n  ]',
                 '  if (charmos_repo != "") {\n    deps = []\n  } else {\n  deps = [\n    "//base:base_unittests",  # There is rust stuff in here.\n    "//build/rust/tests",\n    "//third_party/cloud_authenticator/processor",\n  ]\n  }'),
                ('if (!is_ios && !is_android && !is_castos && !is_cronet_build) {\n  group("blackbox_fuzzing_targets") {',
                 'if (!is_ios && !is_android && !is_castos && !is_cronet_build && !is_charmos) {\n  group("blackbox_fuzzing_targets") {'),
            ]),
        "base/i18n/BUILD.gn": (
            "6a9c036b939de5b877cd2b27660f1a29f9f6c32d26cb55daca786e71cbf9e9e7", [
                ('    "tag_converters.cc",\n    "tag_converters.h",',
                 '    "tag_converters.h",\n    "//build/charmos/tag_converters_charmos.cc",\n    "//build/charmos/language_tag_matcher_charmos.cc",'),
                ('    "language_tag_matcher.cc",\n    "language_tag_matcher.h",',
                 '    "language_tag_matcher.h",'),
                ('    "//third_party/rust/icu_capi/v2:lib",', '    if (!is_charmos) {\n      deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n    }'),
                ('    "//third_party/abseil-cpp:absl",\n    if (!is_charmos) {\n      deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n    }\n  ]',
                 '    "//third_party/abseil-cpp:absl",\n  ]\n  if (!is_charmos) {\n    deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n  }'),
                ('  if (!is_charmos) {\n    deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n  }',
                 '  if (charmos_repo == "") {\n    deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n  }'),
                ('  public_deps = [ ":language_tag_no_icu" ]',
                 '  public_deps = [ ":language_tag_no_icu", "//third_party/icu" ]'),
            ]),
        "base/i18n/icubridge/BUILD.gn": (
            "f74803c7a3a7ac017b1d2a17dadf3ef659af595a4a62e90de0a37cde59296708", [
                ('    "normalizer.cc",', '    "//build/charmos/normalizer_charmos.cc",'),
                ('    "//third_party/rust/icu_capi/v2:lib",', '    if (!is_charmos) {\n      deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n    }'),
                ('    "//third_party/icu",\n    if (!is_charmos) {\n      deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n    }\n  ]',
                 '    "//third_party/icu",\n  ]\n  if (!is_charmos) {\n    deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n  }'),
                ('  if (!is_charmos) {\n    deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n  }',
                 '  if (charmos_repo == "") {\n    deps += [ "//third_party/rust/icu_capi/v2:lib" ]\n  }'),
            ]),
        "base/i18n/language_tag_matcher.h": (
            "ef7f76c42cebdedb202698d0676dcfdfa0c3af709066b2b513f8c08d2a0c2ad4", [
                ('#include "base/memory/raw_ptr_exclusion.h"',
                 '#include "base/memory/raw_ptr_exclusion.h"\n#include "build/build_config.h"'),
                ('namespace icu4x {\nclass LocaleFallbacker;\n}',
                 'namespace icu { class LocaleMatcher; }\nnamespace icu4x {\nclass LocaleFallbacker;\n}'),
                ('      std::unique_ptr<icu4x::LocaleFallbacker> icu_fallbacker);',
                 '#if BUILDFLAG(IS_CHARMOS)\n      std::unique_ptr<icu::LocaleMatcher> icu_fallbacker);\n#else\n      std::unique_ptr<icu4x::LocaleFallbacker> icu_fallbacker);\n#endif'),
                ('  std::unique_ptr<icu4x::LocaleFallbacker> icu_fallbacker_;',
                 '#if BUILDFLAG(IS_CHARMOS)\n  std::unique_ptr<icu::LocaleMatcher> icu_fallbacker_;\n#else\n  std::unique_ptr<icu4x::LocaleFallbacker> icu_fallbacker_;\n#endif'),
                ('#if BUILDFLAG(IS_CHARMOS)\n      std::unique_ptr<icu::LocaleMatcher> icu_fallbacker);\n#else\n      std::unique_ptr<icu4x::LocaleFallbacker> icu_fallbacker);\n#endif',
                 '      std::unique_ptr<icu::LocaleMatcher> icu_fallbacker);'),
                ('#if BUILDFLAG(IS_CHARMOS)\n  std::unique_ptr<icu::LocaleMatcher> icu_fallbacker_;\n#else\n  std::unique_ptr<icu4x::LocaleFallbacker> icu_fallbacker_;\n#endif',
                 '  std::unique_ptr<icu::LocaleMatcher> icu_fallbacker_;'),
                ('namespace icu { class LocaleMatcher; }',
                 '#include "third_party/icu/source/common/unicode/utypes.h"\nU_NAMESPACE_BEGIN\nclass LocaleMatcher;\nU_NAMESPACE_END'),
            ]),
        "chrome/browser/ui/views/profiles/BUILD.gn": (
            "102ec71031131c6d46ecea4dfbddc0cb722718f6c151e196f6731228c6d24a55", [
                ('if (is_win || is_mac || is_linux || is_chromeos) {',
                 'if (is_win || is_mac || is_linux || is_chromeos || is_charmos) {'),
                ('if (is_win || is_mac || is_linux) {',
                 'if (is_win || is_mac || is_linux || is_charmos) {'),
            ]),
        "build/rust/crubit/BUILD.gn": (
            "c9d2a19973860b3562144ad1056ec9e124445b458ecd37f63c870dc0380fd748", [
                ('  public_deps = [\n    "$crubit_src_dir/support:support_cpp",\n    "$crubit_src_dir/support/rs_std:rs_std_cpp",\n    "//build/rust/std/rules:alloc_bindings",\n    "//build/rust/std/rules:core_bindings",\n    "//build/rust/std/rules:std_bindings",\n  ]',
                 '  public_deps = [\n    "$crubit_src_dir/support:support_cpp",\n    "$crubit_src_dir/support/rs_std:rs_std_cpp",\n  ]\n  if (enable_rust) {\n    public_deps += [\n      "//build/rust/std/rules:alloc_bindings",\n      "//build/rust/std/rules:core_bindings",\n      "//build/rust/std/rules:std_bindings",\n    ]\n  }'),
                ('import("//build_overrides/crubit.gni")',
                 'import("//build_overrides/crubit.gni")\nimport("//build/config/rust.gni")'),
            ]),
        "ui/webui/resources/cr_components/signin/BUILD.gn": (
            "b8642a38c597e1f806e74883eb1017ff9d7a0b105d3db42b7ba25f9adf8810b6", [
                ('assert(enable_dice_support)',
                 'import("//components/signin/features.gni")\nassert(enable_dice_support)'),
            ]),
        "third_party/dawn/src/dawn/common/BUILD.gn": (
            "5ddfe58be80ec142acdcfc5a0b2828a95b6d8562b23aaf798dabd4cc20eeb968", [
                ('    is_ios || is_wasm) {', '    is_ios || is_wasm || is_charmos) {'),
            ]),
        "base/allocator/partition_allocator/BUILD.gn": (
            "23ba2667a54ef366400993991f4b0933b47b230f7eaabb05bda11ee576ccede1", [
                ('if (use_partition_alloc && is_clang_or_gcc) {',
                 'if (!use_partition_alloc && is_clang_or_gcc) {\n  group("partition_alloc") { visibility = [ "*" ] }\n}\nif (use_partition_alloc && is_clang_or_gcc) {'),
            ]),
        "components/cbor/rust/BUILD.gn": (
            "47406a245293ff82ff1c1a8215ca5fd83eb33cccea6ac67f48ca8c164a33748c", [
                ('if (!is_cronet_build) {',
                 'if (!enable_rust) {\n  group("cbor_rust_bindings") { visibility = [ "*" ] }\n}\nif (!is_cronet_build) {'),
            ]),
        "components/web_package/signed_web_bundles/rust/BUILD.gn": (
            "b99b36a0ae79a06436d055f2d625750b34386bec713a26bc2fb35b22e0ade846", [
                ('rust_static_library("signed_web_bundles_rust") {',
                 'if (!enable_rust) {\n  group("signed_web_bundles_rust_bindings") { visibility = [ "*" ] }\n}\nrust_static_library("signed_web_bundles_rust") {'),
            ]),
        "components/private_verification_tokens/common/athm_ffi/BUILD.gn": (
            "8a132867c87ba7ed0ae8a33c8d03cd848169597fdbb303ca134b84b4c7a59539", [
                ('assert(enable_rust)', 'assert(enable_rust || is_charmos)'),
                ('assert(enable_rust || is_charmos)',
                 'if (!enable_rust) {\n  group("athm_ffi_bindings") { visibility = [ "*" ] }\n}\nassert(enable_rust || is_charmos)'),
            ]),
        "chrome/browser/ui/webui/intro/BUILD.gn": (
            "61ec9b8fb42a3a2d8725a53aa82160ce9310c8ace67b3ed0de9c5ef2956145ba", [
                ('if (is_win || is_mac || is_linux) {', 'if (is_win || is_mac || is_linux || is_charmos) {'),
            ]),
        "chrome/browser/ui/webui/BUILD.gn": (
            "9bff25e53a6d2ce1b364c7baf03225915f5c9d02bbd22ca01e5cec96b49e994a", [
                ('if (is_win || is_mac || is_linux) {\n  source_set("profile_helper") {',
                 'if (is_win || is_mac || is_linux || is_charmos) {\n  source_set("profile_helper") {'),
            ]),
        "ui/base/BUILD.gn": (
            "b053e87d068d2f8ed2637d5c6eb15532decf7a9522d09d97e49992b70978ed23", [
                ('if (is_win || is_mac || is_linux || is_chromeos) {\n  static_library("pixel_diff_test_support") {',
                 'if (is_win || is_mac || is_linux || is_chromeos || is_charmos) {\n  static_library("pixel_diff_test_support") {'),
            ]),
        "ui/views/BUILD.gn": (
            "0f6360185a8766f925e375ef6401bcfa994d7c369d4b7a6d48ae2d0a14d7b497", [
                ('if (is_win || is_mac || is_linux || is_chromeos) {\n  static_library("view_pixel_diff_test_support") {',
                 'if (is_charmos) {\n  group("close_bubble_on_tab_activation_helper") { visibility = [ "*" ] }\n}\nif (is_win || is_mac || is_linux || is_chromeos || is_charmos) {\n  static_library("view_pixel_diff_test_support") {'),
                ('if (is_charmos) {\n  group("close_bubble_on_tab_activation_helper") { visibility = [ "*" ] }\n}\n',
                 'group("close_bubble_on_tab_activation_helper") { visibility = [ "*" ] }\n'),
            ]),
        "chrome/browser/ui/webui/compose/BUILD.gn": (
            "28226860ce0c6b9fc3130008657a3d33c54f5cf5fb6404d9ee6724fff9856260", [
                ('if (enable_compose) {', 'if (!enable_compose) {\n  group("compose") { visibility = [ "*" ] }\n}\nif (enable_compose) {'),
            ]),
        "build/rust/BUILD.gn": (
            "fde5d8f16376b35506b7cd9f48c99aa6fda407701885cc3f4f9d43c50ac88e3a", [
                ('# Copyright 2021 The Chromium Authors', 'if (!enable_rust) {\n  group("cxx_cppdeps") { visibility = [ "*" ] }\n  group("cxx_rustdeps") { visibility = [ "*" ] }\n}\n\n# Copyright 2021 The Chromium Authors'),
                ('if (!enable_rust) {\n  group("cxx_cppdeps")',
                 'import("//build/config/rust.gni")\nif (!enable_rust) {\n  group("cxx_cppdeps")'),
            ]),
        "mojo/public/rust/bindings/BUILD.gn": (
            "1e34b4861d99297d283dbd9e4f2f1a46d38bf80fd8d67cc9df6f66cf5f27191a", [
                ('import("//build/rust/rust_static_library.gni")', 'if (!enable_rust) {\n  group("bindings_cxx_generated_gen") { visibility = [ "*" ] }\n}\n\nimport("//build/rust/rust_static_library.gni")'),
                ('if (!enable_rust) {\n  group("bindings_cxx_generated_gen")',
                 'import("//build/config/rust.gni")\nif (!enable_rust) {\n  group("bindings_cxx_generated_gen")'),
            ]),
        "mojo/public/rust/bindings/test/BUILD.gn": (
            "3bb609e8107e3ef997947dd7b3cd0b3bdee7d1d79a417a40d1a6a50bad3fec96", [
                ('import("//build/rust/rust_static_library.gni")', 'if (!enable_rust) {\n  group("bindings_unittests_cxx_generated_gen") { visibility = [ "*" ] }\n}\n\nimport("//build/rust/rust_static_library.gni")'),
                ('if (!enable_rust) {\n  group("bindings_unittests_cxx_generated_gen")',
                 'import("//build/config/rust.gni")\nif (!enable_rust) {\n  group("bindings_unittests_cxx_generated_gen")'),
            ]),
        "third_party/blink/renderer/platform/fonts/BUILD.gn": (
            "0f2f96d43732dbbdcf6af65cff68c1bb8e2e4efa5b84a21ff8cddd99475e362d", [
                ('# Copyright 2026 The Chromium Authors', 'if (!enable_rust) {\n  group("font_format_bindings") { visibility = [ "*" ] }\n  group("ift_patcher_bindings") { visibility = [ "*" ] }\n}\n\n# Copyright 2026 The Chromium Authors'),
                ('import("//build/rust/rust_static_library.gni")',
                 'import("//build/config/rust.gni")\nimport("//build/rust/rust_static_library.gni")'),
                ('if (!enable_rust) {\n  group("font_format_bindings")',
                 'import("//build/config/rust.gni")\nif (!enable_rust) {\n  group("font_format_bindings")'),
            ]),
        "chrome/browser/ui/views/bubble/BUILD.gn": (
            "4ebe0278f6b0abb0695ae73f3948c9834fd0c922230dece0c4b444f413c59f67", [
                ('assert(is_win || is_mac || is_linux || is_chromeos)',
                 'assert(is_win || is_mac || is_linux || is_chromeos || is_charmos)'),
                ('    "//chrome/browser/ui/views:close_bubble_on_tab_activation_helper",\n', ''),
            ]),
        "content/shell/BUILD.gn": (
            "8b952574b939aa81d0dba0113206efa708b3f1c3718ef874aadc2a7b2a4eb13e", [
                ('      "//third_party/breakpad:dump_syms",\n', ''),
                ('    "//v8",\n'
                 '  ]\n'
                 '\n'
                 '  if (is_fuchsia) {',
                 '    "//v8",\n'
                 '  ]\n'
                 '\n'
                 '  if (is_charmos) {\n'
                 '    deps -= [\n'
                 '      ":rust_test_mojom",\n'
                 '      ":rust_test_mojom_js",\n'
                 '      ":rust_test_service_ffi",\n'
                 '      "//components/custom_handlers:test_support",\n'
                 '      "//components/services/storage/test_api",\n'
                 '      "//content/test:content_test_mojo_bindings",\n'
                 '      "//content/test:test_support",\n'
                 '      "//services/device/public/cpp:test_support",\n'
                 '      "//services/test/echo:lib",\n'
                 '      "//services/test/echo/public/mojom",\n'
                 '    ]\n'
                 '  }\n'
                 '\n'
                 '  if (is_fuchsia) {'),
                ('      "//ui/aura",\n      "//ui/aura:test_support",',
                 '      "//ui/aura",'),
            ]),
        "content/web_test/BUILD.gn": (
            "be109f320f21701ef711102eb292d884aab80a5c3f8e3c704c1824c112739826", [
                ('    "//skia:test_fonts",\n', ''),
            ]),
        "third_party/blink/renderer/platform/heap/BUILD.gn": (
            "56d50ef818d6693a929a93f34bd4237ceeecc4f0dfeb686d596a59ab240e621b", [
                ('    "//skia:test_fonts",\n', ''),
            ]),
        "tools/perf/chrome_telemetry_build/BUILD.gn": (
            "ab6063d75cfa81d0f6a2e3dcf85943746d45b208531ae2d9039cf441e66b6c83", [
                ('  data_deps += [ "//third_party/breakpad:dump_syms" ]', ''),
                ('      "//chrome:chrome_helper_app_default",\n      "//third_party/breakpad:dump_syms",',
                 '      "//chrome:chrome_helper_app_default",'),
            ]),
        "third_party/breakpad/BUILD.gn": (
            "137514498a81f98ee0f652e1314966d69e77ff0a141d2918b97ea6ce84522ac8", [
                ('# Copyright 2014 The Chromium Authors', 'if (is_charmos) {\n  group("dump_syms") { visibility = [ "*" ] }\n}\n\n# Copyright 2014 The Chromium Authors'),
                ('  } else if (current_toolchain == default_toolchain) {\n    # The default toolchain builds the system-allocator binaries, which are\n    # placed in the output dir of the default toolchain.\n    copy_exe("microdump_stackwalk") {',
                 '  } else if (current_toolchain == default_toolchain && !is_charmos) {\n    # The default toolchain builds the system-allocator binaries, which are\n    # placed in the output dir of the default toolchain.\n    copy_exe("microdump_stackwalk") {'),
                ('  } else if (current_toolchain == default_toolchain && !is_charmos) {\n    # The default toolchain builds the system-allocator binaries, which are\n    # placed in the output dir of the default toolchain.\n    copy_exe("microdump_stackwalk") {',
                 '  } else if (current_toolchain == default_toolchain) {\n    # The default toolchain builds the system-allocator binaries, which are\n    # placed in the output dir of the default toolchain.\n    copy_exe("microdump_stackwalk") {'),
                ('    if (invoker.has_exe_suffix) {\n      _exe = ".exe"',
                 '    if (is_charmos) {\n      _exe = ".elf"\n    } else if (invoker.has_exe_suffix) {\n      _exe = ".exe"'),
                ('      _exe = ".elf"\n    } else if (invoker.has_exe_suffix) {',
                 '      _exe = ".elf"\n      not_needed(invoker, [ "has_exe_suffix" ])\n    } else if (invoker.has_exe_suffix) {'),
            ]),
        "components/test/data/update_client/puffin_patch_test/BUILD.gn": (
            "160277eefa1cbb0576fa0fd4f2ede055480c9b915ea191960cf794a1438cfa1f", [
                ('  deps = [ ":puffin_app_v1_binary" ]\n  if (is_win) {', '  deps = [ ":puffin_app_v1_binary" ]\n  if (is_charmos) {\n    inputs = [ "$root_build_dir/puffin_app_v1_binary.elf" ]\n  } else if (is_win) {'),
                ('  deps = [ ":puffin_app_v2_binary" ]\n  if (is_win) {', '  deps = [ ":puffin_app_v2_binary" ]\n  if (is_charmos) {\n    inputs = [ "$root_build_dir/puffin_app_v2_binary.elf" ]\n  } else if (is_win) {'),
            ]),
    }
    # Preserve the staged transformations above so previously prepared trees
    # remain recognizable. Review fixes must never accept unknown vendor edits.
    patches["net/cert/cert_verifier.cc"][1].append((
        '#elif BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n    return CertVerifyProc::CreateBuiltinVerifyProc(',
        '#elif BUILDFLAG(IS_FUCHSIA)\n    return CertVerifyProc::CreateBuiltinVerifyProc('))
    patches["net/cert/cert_verify_proc.cc"][1].extend([
        ('#if !(BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(CHROME_ROOT_STORE_ONLY) || BUILDFLAG(IS_CHARMOS))',
         '#if !(BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(CHROME_ROOT_STORE_ONLY))'),
        ('#if BUILDFLAG(IS_CHROMEOS)\n      CreateChromeOnlySystemTrustStore(std::move(chrome_root)),',
         '#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_CHARMOS)\n      CreateChromeOnlySystemTrustStore(std::move(chrome_root)),'),
    ])
    patches["ipc/param_traits_utils.cc"][1].extend([
        ('  // VMO capabilities travel over native channels, not legacy Pickle\n'
         '  // attachments. Write no handle; Read reconstructs an empty region.',
         '  // The legacy writer has no failure result. Refuse unsupported valid\n'
         '  // regions rather than serialize a successful but empty transfer.\n'
         '  NOTREACHED() << "Use native Mojo capability transport for VMO IPC";'),
        ('#elif BUILDFLAG(IS_CHARMOS)\n  *r = base::subtle::PlatformSharedMemoryRegion();',
         '#elif BUILDFLAG(IS_CHARMOS)\n'
         '  LOG(ERROR) << "Unsupported legacy VMO attachment of size " << size;\n'
         '  return false;'),
    ])
    patches["ui/gfx/native_pixmap_handle.cc"][1].append((
        '    // Software rendering: native pixmap planes are not used.',
        '    LOG(ERROR) << "Native GPU pixmap cloning is unsupported (plane size "\n'
        '               << plane.size << ")";\n'
        '    return NativePixmapHandle();'))
    patches["ui/base/dragdrop/os_exchange_data_provider_factory.cc"][1].append((
        '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n#include',
        '#if BUILDFLAG(IS_CHARMOS)\n'
        '#include "ui/base/dragdrop/os_exchange_data_provider_non_backed.h"\n'
        '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n#include'))
    patches["ui/gfx/mojom/native_handle_types_mojom_traits.cc"][1].extend([
        ('#elif BUILDFLAG(IS_CHARMOS)\n  return mojo::PlatformHandle();',
         '#elif BUILDFLAG(IS_CHARMOS)\n'
         '  NOTREACHED() << "Native GPU pixmap serialization is unsupported";'),
        ('  out->vmo = zx::vmo(handle.TakeHandle());\n#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)',
         '  out->vmo = zx::vmo(handle.TakeHandle());\n'
         '#elif BUILDFLAG(IS_CHARMOS)\n'
         '  return false;\n'
         '#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)'),
    ])
    patches["components/variations/proto/study.proto"] = (
        "0c126ae211fd4cddff58d978c5fa8ed5b77bcc5dfd25046e22c1a36bfef58fc7", [
            ('    PLATFORM_FUCHSIA = 7;',
             '    PLATFORM_FUCHSIA = 7;\n'
             '    // Downstream native platform; never claim to be Linux.\n'
             '    PLATFORM_CHARMOS = 1000;'),
        ])
    patches["components/variations/client_filterable_state.cc"] = (
        "ab33f0956fdbc3a2292e80fe031c1e67db96c9224cb8c57c7f97943c8f5368cc", [
            ('#elif BUILDFLAG(IS_FUCHSIA)\n  return Study::PLATFORM_FUCHSIA;',
             '#elif BUILDFLAG(IS_CHARMOS)\n  return Study::PLATFORM_CHARMOS;\n'
             '#elif BUILDFLAG(IS_FUCHSIA)\n  return Study::PLATFORM_FUCHSIA;'),
        ])
    patches["crypto/BUILD.gn"][1].append((
        '  if (is_charmos) {\n    sources -= [ "tpm_parser.cc", "tpm_parser.h", "unexportable_key_software_unsecure.cc" ]',
        '  if (is_charmos || !enable_rust) {\n    sources -= [ "tpm_parser.cc", "tpm_parser.h", "unexportable_key_software_unsecure.cc" ]'))
    patches["ipc/param_traits_utils.cc"][1].append((
        '#endif  // BUILDFLAG(IS_ANDROID)\n\n#endif\n\n  return true;\n}\n\n'
        'void ParamTraits<base::subtle::PlatformSharedMemoryRegion::Mode>::Write(',
        '#endif  // BUILDFLAG(IS_ANDROID)\n\n#endif\n\n'
        '#if !BUILDFLAG(IS_CHARMOS)\n  return true;\n#endif\n}\n\n'
        'void ParamTraits<base::subtle::PlatformSharedMemoryRegion::Mode>::Write('))
    patches["ui/gfx/native_pixmap_handle.cc"][1].extend([
        ('  for (auto& plane : handle.planes) {',
         '#if BUILDFLAG(IS_CHARMOS)\n'
         '  if (!handle.planes.empty()) {\n'
         '    LOG(ERROR) << "Native GPU pixmap cloning is unsupported";\n'
         '    return NativePixmapHandle();\n  }\n'
         '#else\n  for (auto& plane : handle.planes) {'),
        ('  }\n\n#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n  clone.modifier',
         '  }\n#endif  // !IS_CHARMOS\n\n'
         '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n  clone.modifier'),
    ])
    patches["ui/gfx/mojom/native_handle_types_mojom_traits.cc"][1].append((
        '#elif BUILDFLAG(IS_CHARMOS)\n  return false;\n'
        '#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n\n  return true;',
        '#elif BUILDFLAG(IS_CHARMOS)\n  // Native GPU pixmaps are not available.\n'
        '#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n\n'
        '  return !BUILDFLAG(IS_CHARMOS);'))
    patches["third_party/abseil-cpp/absl/base/internal/low_level_alloc.h"] = (
        "0f7dd074b79f49b57c2491987e722dbc0301018937e2a0573e10b3df8afe0649", [
            ('#elif !defined(ABSL_HAVE_MMAP) && !defined(_WIN32)',
             '// Native anonymous mmap is VMO-backed; the allocator uses only\n'
             '// this supported subset, not POSIX fixed/file mappings.\n'
             '#elif !defined(ABSL_HAVE_MMAP) && !defined(_WIN32) && !defined(__charmos__)'),
            ('    defined(__hexagon__)\n#define ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING 1',
             '    defined(__hexagon__) || defined(__charmos__)\n'
             '#define ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING 1'),
        ])
    patches["base/files/memory_mapped_file.h"] = (
        "04d831506011df4ff55a0850cb3f6b8dc08ba14f204505cdf305ef238fe4d5cb", [
            ('  RAW_PTR_EXCLUSION span<uint8_t> bytes_;',
             '  RAW_PTR_EXCLUSION span<uint8_t> bytes_;\n'
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  // Keep the original mapping, not an unaligned subregion view.\n'
             '  RAW_PTR_EXCLUSION void* charmos_mapping_base_ = nullptr;\n'
             '  size_t charmos_mapping_size_ = 0;\n#endif'),
        ])
    patches["base/files/memory_mapped_file_posix.cc"] = (
        "d15c39324f9d39a1b8f71cb97f0175373495df50900327227ebeb1ef88b6fddd", [
            ('  off_t map_start = 0;',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  // Shared/writeback mappings are not supported by the native ABI.\n'
             '  if (access == READ_WRITE || access == READ_WRITE_EXTEND) return false;\n'
             '  if (region != Region::kWholeFile &&\n'
             '      (region.offset < 0 || !region.size || region.size > static_cast<uint64_t>(INT64_MAX - region.offset)))\n'
             '    return false;\n#endif\n\n  off_t map_start = 0;'),
            ('  int prot = 0;',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  // Native private mappings copy only existing file bytes. Clamp\n'
             '  // page-alignment padding at EOF while retaining the full view.\n'
             '  int64_t native_length = file_.GetLength();\n'
             '  if (native_length < 0 || map_start > native_length || data_offset < 0) return false;\n'
             '  uint64_t available = static_cast<uint64_t>(native_length - map_start);\n'
             '  if (static_cast<uint64_t>(data_offset) > available ||\n'
             '      byte_size > available - static_cast<uint64_t>(data_offset)) return false;\n'
             '  if (map_size > available) map_size = static_cast<size_t>(available);\n'
             '#endif\n\n  int prot = 0;'),
            ('    case READ_ONLY:\n      prot |= PROT_READ;\n      break;',
             '    case READ_ONLY:\n      prot |= PROT_READ;\n'
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '      // A genuine VMO-backed private snapshot; no shared-file facade.\n'
             '      flags = MAP_PRIVATE;\n#endif\n      break;'),
            ('  bytes_ = UNSAFE_BUFFERS(base::span(ptr + data_offset, byte_size));',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  charmos_mapping_base_ = ptr;\n  charmos_mapping_size_ = map_size;\n#endif\n'
             '  bytes_ = UNSAFE_BUFFERS(base::span(ptr + data_offset, byte_size));'),
            ('  if (!bytes_.empty()) {\n    munmap(bytes_.data(), bytes_.size());\n  }',
             '#if BUILDFLAG(IS_CHARMOS)\n'
             '  if (charmos_mapping_base_) {\n'
             '    munmap(charmos_mapping_base_, charmos_mapping_size_);\n'
             '    charmos_mapping_base_ = nullptr;\n    charmos_mapping_size_ = 0;\n  }\n'
             '#else\n  if (!bytes_.empty()) {\n    munmap(bytes_.data(), bytes_.size());\n  }\n#endif'),
        ])
    # Native libc and the linker script live outside Chromium's target graph.
    # Let LLD record every real linker input so Ninja sees archive/script
    # changes without recompiling all C++ translation units.
    patches["build/toolchain/gcc_toolchain.gni"] = (
        "45cd14bf6281cce2764da2eaf2ef0f653f2410ee622c7aad11223bba8b24dc1e", [
            ('      link_command = "$ld {{ldflags}}${extra_ldflags} -o \\"$main_outfile\\" "\n',
             '      link_command = "$ld {{ldflags}}${extra_ldflags} -o \\"$main_outfile\\" "\n'
             '      if (current_os == "charmos") {\n'
             '        depfile = "$outfile.d"\n'
             '        link_command += " \\"-Wl,--dependency-file=$depfile\\" "\n'
             '      }\n'),
            ('      if (current_os == "charmos") {\n'
             '        depfile = "$outfile.d"\n',
             '      if (invoker_toolchain_args.current_os == "charmos") {\n'
             '        depfile = "$outfile.d"\n'),
        ])
    return patches
