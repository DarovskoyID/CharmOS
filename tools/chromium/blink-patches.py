"""No-Rust Blink profile: use the existing libxml2 XML parser.

Chromium 155 migrated XML parsing to a Rust `xml_ffi` crate. Blink still ships
the C++/libxml2 parser, so CharmOS keeps that path and removes the Rust target
from the production closure (the user requirement is C++ only).
"""

def blink_patches():
    return {
        # CharmOS is C++ only: replace Chromium 155's Rust MPEG/JPEG parsers
        # with the platform C++ implementations.
        "media/formats/mpeg/mpeg_audio_stream_parser_base.cc": (
            "c33852fcb9bf2e084c59898608b644425ed2de5199e42ce2ac9af24bde5f6990", [
                ('#include "media/formats/mpeg/lib.rs.h"\n',
                 '#include "build/charmos/mpeg_audio_ffi.h"\n'),
                ('    auto rust_data = ::rust::Slice<const uint8_t>(active_data);\n'
                 '    auto action = audio_codec_ == AudioCodec::kAAC\n'
                 '                      ? media::formats::mpeg::parse_adts_action(rust_data)\n'
                 '                      : media::formats::mpeg::parse_mp3_action(rust_data);',
                 '    auto action = audio_codec_ == AudioCodec::kAAC\n'
                 '                      ? media::formats::mpeg::parse_adts_action(active_data)\n'
                 '                      : media::formats::mpeg::parse_mp3_action(active_data);'),
            ]),
        "media/formats/mpeg/mpeg1_audio_stream_parser.cc": (
            "094838ed70cb479af8669ca7ee84e047d057b36fa8764f6b9f4692384a6e8063", [
                ('#include "media/formats/mpeg/lib.rs.h"\n',
                 '#include "build/charmos/mpeg_audio_ffi.h"\n'),
                ('  auto rust_data = rust::Slice<const uint8_t>(data);\n'
                 '  auto ffi_res = media::formats::mpeg::parse_mp3_header(rust_data);',
                 '  auto ffi_res = media::formats::mpeg::parse_mp3_header(data);'),
            ]),
        "media/formats/mpeg/adts_stream_parser.cc": (
            "ca37e566bb97cdfa74c1923ed2ed6cb680ed15ed0803f9ab0ef07aac4ff8a590", [
                ('#include "media/formats/mpeg/lib.rs.h"\n',
                 '#include "build/charmos/mpeg_audio_ffi.h"\n'),
                ('  auto rust_data = rust::Slice<const uint8_t>(data);\n'
                 '  auto ffi_res = media::formats::mpeg::parse_adts_header(rust_data);',
                 '  auto ffi_res = media::formats::mpeg::parse_adts_header(data);'),
            ]),
        "media/parsers/jpeg_parser.cc": (
            "7396a5796d89cf5719f14944aaa7fbb9806a7c28fadf04cf10f9dcf94faed05a", [
                ('#include "media/parsers/parse_jpeg.rs.h"\n',
                 '#include "build/charmos/jpeg_parser_ffi.h"\n'),
                ('  auto ffi_res = parsers::parse_jpeg_picture_ffi(\n'
                 '      rust::Slice<const uint8_t>(buffer.data(), buffer.size()));',
                 '  auto ffi_res = parsers::parse_jpeg_picture_ffi(buffer);'),
            ]),
        "media/parsers/BUILD.gn": (
            "bfccab3d9cdc99e4303f2ca747247f185113c9762efb161f0987e00e0f9de4a3", [
                ('import("//build/rust/rust_static_library.gni")\n', ''),
                ('rust_static_library("safe_parsers") {\n'
                 '  crate_root = "parse_jpeg.rs"\n'
                 '  sources = [ "parse_jpeg.rs" ]\n'
                 '  cxx_bindings = [ "parse_jpeg.rs" ]\n'
                 '\n'
                 '  # Required by Chromium policy because cxx generates unsafe code under the\n'
                 '  # hood.\n'
                 '  allow_unsafe = true\n'
                 '}\n'
                 '\n', ''),
                ('    "jpeg_parser.cc",\n    "jpeg_parser.h",\n',
                 '    "jpeg_parser.cc",\n    "jpeg_parser.h",\n'
                 '    "//build/charmos/jpeg_parser_ffi.cc",\n'),
                ('    ":safe_parsers",\n', ''),
            ]),
        "media/formats/BUILD.gn": (
            "9bd27113b51d63220c653f38e6023cf32634a01bcdeb355168820bc9e342c02d", [
                ('import("//build/rust/rust_static_library.gni")\n', ''),
                ('rust_static_library("safe_mpeg_audio_parsers") {\n'
                 '  crate_root = "mpeg/lib.rs"\n'
                 '  sources = [\n'
                 '    "mpeg/bit_reader.rs",\n'
                 '    "mpeg/lib.rs",\n'
                 '    "mpeg/mpeg_audio_parser.rs",\n'
                 '    "mpeg/parse_adts.rs",\n'
                 '    "mpeg/parse_mp3.rs",\n'
                 '  ]\n'
                 '  cxx_bindings = [ "mpeg/lib.rs" ]\n'
                 '  allow_unsafe = true\n'
                 '}\n'
                 '\n'
                 'rust_static_library("rust_unittests") {\n'
                 '  testonly = true\n'
                 '  is_gtest_unittests = true\n'
                 '  crate_root = "mpeg/bit_reader_unittest.rs"\n'
                 '  sources = [ "mpeg/bit_reader_unittest.rs" ]\n'
                 '  deps = [\n'
                 '    ":safe_mpeg_audio_parsers",\n'
                 '    "//testing/rust_gtest_interop",\n'
                 '  ]\n'
                 '}\n'
                 '\n', ''),
                ('    "mpeg/mpeg_audio_stream_parser_base.h",\n',
                 '    "mpeg/mpeg_audio_stream_parser_base.h",\n'
                 '    "//build/charmos/mpeg_audio_ffi.cc",\n'),
                ('    ":safe_mpeg_audio_parsers",\n', ''),
                ('    ":rust_unittests",\n', ''),
            ]),
        "gpu/config/gpu_test_config.cc": (
            "4273cd88ca3c2808b7a0740a1cf1da61f283699c3b7bff106633ad116be6176f", [
                ('#elif BUILDFLAG(IS_IOS)\n'
                 '  return GPUTestConfig::kOsIOS;\n'
                 '#else\n'
                 '#error "unknown os"\n'
                 '#endif',
                 '#elif BUILDFLAG(IS_IOS)\n'
                 '  return GPUTestConfig::kOsIOS;\n'
                 '#else\n'
                 '  #if BUILDFLAG(IS_CHARMOS)\n'
                 '  return GPUTestConfig::kOsLinux;\n'
                 '  #else\n'
                 '  #error "unknown os"\n'
                 '  #endif\n'
                 '#endif'),
            ]),
        "ui/base/clipboard/clipboard_constants.h": (
            "9820a6034ca0fe764bbec1fefc650c6ea2ebcd14a744a02aa00791eb2eebc466", [
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n'
                 'inline constexpr char kMimeTypeLinuxUtf8String[] = "UTF8_STRING";',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'
                 'inline constexpr char kMimeTypeLinuxUtf8String[] = "UTF8_STRING";'),
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_ANDROID)\n'
                 'inline constexpr char kMimeTypeSourceUrl[] = "chromium/x-source-url";',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHARMOS)\n'
                 'inline constexpr char kMimeTypeSourceUrl[] = "chromium/x-source-url";'),
            ]),
        "media/capture/video/fake_video_capture_device_factory.cc": (
            "36bfad0f39d35ac30f8c0824313ef2c6cb1922e9974acfcb1749ccf58e44929b", [
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 '        VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE;',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 '        VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE;'),
                ('#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '        VideoCaptureApi::FUCHSIA_CAMERA3;\n'
                 '#else\n'
                 '#error Unsupported platform\n'
                 '#endif',
                 '#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '        VideoCaptureApi::FUCHSIA_CAMERA3;\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '        VideoCaptureApi::UNKNOWN;\n'
                 '#else\n'
                 '#error Unsupported platform\n'
                 '#endif'),
            ]),
        "third_party/iamf_tools/src/iamf/common/read_bit_buffer.cc": (
            "1fa256493a2295ecc19a311baac65f33587623f910c99190eaffcc4f43f15fd4", [
                ('  if (!std::filesystem::exists(file_path)) {',
                 '  if (file_path.empty()) {'),
            ]),
        "components/variations/field_trial_config/BUILD.gn": (
            "d773515d272c1200d8de24adceeb6cac22bc247fd03fc8e16a12371390a16974", [
                ('  if (current_os == "win") {\n'
                 '    platform = "windows"\n'
                 '  } else {\n'
                 '    platform = current_os\n'
                 '  }',
                 '  if (current_os == "win") {\n'
                 '    platform = "windows"\n'
                 '  } else if (current_os == "charmos") {\n'
                 '    # CharmOS uses the Linux variation targets.\n'
                 '    platform = "linux"\n'
                 '  } else {\n'
                 '    platform = current_os\n'
                 '  }'),
            ]),
        "third_party/angle/src/common/SimpleMutex.h": (
            "06679a0132866e8dea8f3c1ec42c90cb62caeed0465913d14ab122785dbc5890", [
                ('#    if defined(ANGLE_PLATFORM_LINUX) || defined(ANGLE_PLATFORM_ANDROID)\n'
                 '// Linux has had futexes for a very long time.  Assume support.\n'
                 '#        define ANGLE_USE_FUTEX 1',
                 '#    if (defined(ANGLE_PLATFORM_LINUX) && !defined(__charmos__)) || \\\n'
                 '        defined(ANGLE_PLATFORM_ANDROID)\n'
                 '// Linux has had futexes for a very long time.  Assume support.\n'
                 '#        define ANGLE_USE_FUTEX 1'),
            ]),
        "third_party/blink/common/chrome_debug_urls.cc": (
            "3e92ad39a1adb74e9a0a673ec6010657f6a7b4476f6140c121d7929b956839ab", [
                ('#include "third_party/blink/common/rust_crash/src/lib.rs.h"\n', ''),
                ('crash_in_rust_with_overflow();', 'internal::CrashIntentionally();'),
                ('crash_in_rust();', 'internal::CrashIntentionally();'),
            ]),
        "third_party/blink/renderer/platform/wtf/stack_util.cc": (
            "367145d4e49da18064bdeddd4567e42e53f67ddced463508f684357c5e7d3af4", [
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\\n'
                 '    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FREEBSD) || BUILDFLAG(IS_FUCHSIA)\n',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\\n'
                 '    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FREEBSD) || BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'),
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || \\\n'
                 '    BUILDFLAG(IS_FREEBSD) || BUILDFLAG(IS_FUCHSIA)\n',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || \\\n'
                 '    BUILDFLAG(IS_FREEBSD) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n'),
            ]),
        "gpu/config/gpu_driver_bug_workaround_type.h": (
            "2cab31148b80896a7033ed10fc03b63ec7f646fe251a1ceff41892c14b0e6d23", [
                ('#include "gpu/config/gpu_config_export.h"\n'
                 '#include "gpu/config/gpu_driver_bug_workaround_autogen.h"',
                 '#include "gpu/config/gpu_config_export.h"\n'
                 '// Skia may have defined the same helper macro with its narrower\n'
                 '// workaround list before this header is reached.\n'
                 '#undef GPU_DRIVER_BUG_WORKAROUNDS\n'
                 '#include "gpu/config/gpu_driver_bug_workaround_autogen.h"'),
            ]),
        "gpu/command_buffer/service/shared_image/shared_image_factory.cc": (
            "8e273df3aa08637d0718c766e2588e6cb8a0357e96a1b15a181d4ab613c09929", [
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n'
                 '    case gfx::NATIVE_PIXMAP:\n'
                 '      return "platform";\n'
                 '#endif',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_CHARMOS)\n'
                 '    case gfx::NATIVE_PIXMAP:\n'
                 '      return "platform";\n'
                 '#endif'),
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n'
                 '  return gfx::GpuMemoryBufferType::NATIVE_PIXMAP;',
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return gfx::GpuMemoryBufferType::SHARED_MEMORY_BUFFER;\n'
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_FUCHSIA)\n'
                 '  return gfx::GpuMemoryBufferType::NATIVE_PIXMAP;'),
            ]),
        "third_party/tflite_support/src/tensorflow_lite_support/cc/task/core/external_file_handler.cc": (
            "75ef020892c2546d270c6256e6ed3aee0187d481147d3003ed7b7411ba015d1d", [
                ('#ifdef ABSL_HAVE_MMAP\n#include <sys/mman.h>\n#endif',
                 '#if defined(ABSL_HAVE_MMAP) || !defined(_WIN32)\n'
                 '#include <sys/mman.h>\n#endif'),
            ]),
        "third_party/libxslt/BUILD.gn": (
            "b110b5f7f8adf4ba550556bd99cd9d5ada2d39d72d29dba8c87c0f64fcb92b06", [
                ('  if (is_linux || is_chromeos) {\n'
                 '    sources += [ "linux/config.h" ]',
                 '  if (is_linux || is_chromeos || is_charmos) {\n'
                 '    sources += [ "linux/config.h" ]'),
                ('  if (is_linux || is_chromeos || is_android || is_fuchsia) {\n'
                 '    include_dirs = [ "linux" ]',
                 '  if (is_linux || is_chromeos || is_android || is_fuchsia || is_charmos) {\n'
                 '    include_dirs = [ "linux" ]'),
            ]),
        "ui/gfx/native_pixmap_handle.h": (
            "80f35fdf1aa62e464252ce5ba6515a2188ce137a0017e84b06fab9ec5d66d43c", [
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 '  // The modifier is retrieved from GBM library and passed to EGL driver.',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 '  // The modifier is retrieved from GBM library and passed to EGL driver.'),
            ]),
        "gpu/ipc/service/gpu_init.cc": (
            "4f025751c3c11c7045c3ec82125eaba61ecf2d3cec8978bb7143b574aca34242", [
                ('  bool filter_set = false;',
                 '  [[maybe_unused]] bool filter_set = false;'),
            ]),
        "media/webrtc/helpers.cc": (
            "d11563f636d44be6f3e477d87ca2499250fc58451f8832c36c8ac1f3df5dde2f", [
                ('#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)\n'
                 '  const bool kInputVolumeAdjustmentOverrideAllowed = true;',
                 '#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'
                 '  const bool kInputVolumeAdjustmentOverrideAllowed = true;'),
                ('#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n'
                 '  // Use AGC2 digital and input volume controller.',
                 '#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'
                 '  // Use AGC2 digital and input volume controller.'),
            ]),
        "third_party/blink/renderer/platform/fonts/opentype/font_format_check.h": (
            "927ce3258ab7a8584aefeefda05a165baa8a2631cf972074ce4cee0391e3cfa3", [
                ('#include "third_party/blink/renderer/platform/fonts/font_format.h"\n', ''),
                (' private:\n  font_format::FontFormatInfo format_info_;\n};',
                 ' private:\n'
                 '  bool is_variable_ = false;\n'
                 '  bool is_cbdt_cblc_ = false;\n'
                 '  bool is_ebdt_eblc_ = false;\n'
                 '  bool is_colrv0_ = false;\n'
                 '  bool is_colrv1_ = false;\n'
                 '  bool is_sbix_ = false;\n'
                 '  bool is_cff2_ = false;\n'
                 '  bool is_avar2_ = false;\n'
                 '};'),
            ]),
        "third_party/blink/renderer/platform/fonts/opentype/font_format_check.cc": (
            "a574ddabcfbe7699f25b6ef53be871a46e96b674e7ac3546a1c8dbffe5b7a16c", [
                ('#include "base/containers/span.h"\n'
                 '#include "base/containers/span_rust.h"\n',
                 '#include <map>\n'
                 '#include <set>\n'
                 '\n'
                 '#include "base/containers/span.h"\n'),
                ('FontFormatCheck::FontFormatCheck(sk_sp<SkData> sk_data)\n'
                 '    : format_info_(font_format::get_font_format_info(\n'
                 '          base::SpanToRustSlice(sk_data->byteSpan()))) {}',
                 'namespace {\n'
                 '\n'
                 'uint16_t ReadFontU16(base::span<const uint8_t> data, size_t offset) {\n'
                 '  return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);\n'
                 '}\n'
                 '\n'
                 'uint32_t ReadFontU32(base::span<const uint8_t> data, size_t offset) {\n'
                 '  return (static_cast<uint32_t>(data[offset]) << 24) |\n'
                 '         (static_cast<uint32_t>(data[offset + 1]) << 16) |\n'
                 '         (static_cast<uint32_t>(data[offset + 2]) << 8) |\n'
                 '         static_cast<uint32_t>(data[offset + 3]);\n'
                 '}\n'
                 '\n'
                 '}  // namespace\n'
                 '\n'
                 'FontFormatCheck::FontFormatCheck(sk_sp<SkData> sk_data) {\n'
                 '  if (!sk_data) {\n'
                 '    return;\n'
                 '  }\n'
                 '  base::span<const uint8_t> data(sk_data->bytes(), sk_data->size());\n'
                 '  if (data.size() < 12) {\n'
                 '    return;\n'
                 '  }\n'
                 '  size_t directory_offset = 0;\n'
                 '  uint32_t signature = ReadFontU32(data, 0);\n'
                 '  if (signature == 0x74746366u) {  // TTC: use the first font.\n'
                 '    if (data.size() < 16) {\n'
                 '      return;\n'
                 '    }\n'
                 '    directory_offset = ReadFontU32(data, 12);\n'
                 '    if (directory_offset > data.size() ||\n'
                 '        data.size() - directory_offset < 12) {\n'
                 '      return;\n'
                 '    }\n'
                 '    signature = ReadFontU32(data, directory_offset);\n'
                 '  }\n'
                 '  if (signature != 0x74727565u && signature != 0x4f54544fu) {\n'
                 '    return;\n'
                 '  }\n'
                 '  const size_t table_directory = directory_offset + 12;\n'
                 '  const uint16_t num_tables = ReadFontU16(data, directory_offset + 4);\n'
                 '  if (num_tables > (data.size() - table_directory) / 16) {\n'
                 '    return;\n'
                 '  }\n'
                 '  std::set<uint32_t> tags;\n'
                 '  std::map<uint32_t, size_t> table_offsets;\n'
                 '  for (uint16_t i = 0; i < num_tables; ++i) {\n'
                 '    const size_t record = table_directory + i * 16;\n'
                 '    const uint32_t tag = ReadFontU32(data, record);\n'
                 '    tags.insert(tag);\n'
                 '    table_offsets[tag] = ReadFontU32(data, record + 8);\n'
                 '  }\n'
                 '\n'
                 '  is_variable_ = tags.count(0x66766172u) != 0;  // fvar\n'
                 '  is_sbix_ = tags.count(0x73626978u) != 0;      // sbix\n'
                 '  is_cbdt_cblc_ = tags.count(0x43424454u) != 0 &&  // CBDT\n'
                 '                  tags.count(0x43424c43u) != 0;    // CBLC\n'
                 '  is_ebdt_eblc_ = tags.count(0x45424454u) != 0 &&  // EBDT\n'
                 '                  tags.count(0x45424c43u) != 0;    // EBLC\n'
                 '  is_cff2_ = tags.count(0x43464632u) != 0;  // CFF2\n'
                 '\n'
                 '  auto colr = table_offsets.find(0x434f4c52u);  // COLR\n'
                 '  if (colr != table_offsets.end() &&\n'
                 '      colr->second + 2 <= data.size()) {\n'
                 '    const uint16_t version = ReadFontU16(data, colr->second);\n'
                 '    is_colrv0_ = version == 0;\n'
                 '    is_colrv1_ = version == 1;\n'
                 '  }\n'
                 '  auto avar = table_offsets.find(0x61766172u);  // avar\n'
                 '  if (avar != table_offsets.end() &&\n'
                 '      avar->second + 4 <= data.size()) {\n'
                 '    is_avar2_ = ReadFontU16(data, avar->second) == 2;\n'
                 '  }\n'
                 '}'),
                ('bool FontFormatCheck::IsVariableFont() const {\n'
                 '  return font_format::is_variable(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsVariableFont() const { return is_variable_; }'),
                ('bool FontFormatCheck::IsCbdtCblcColorFont() const {\n'
                 '  return font_format::is_cbdt_cblc(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsCbdtCblcColorFont() const {\n'
                 '  return is_cbdt_cblc_;\n'
                 '}'),
                ('bool FontFormatCheck::IsEbdtEblcMonochromeFont() const {\n'
                 '  return font_format::is_ebdt_eblc(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsEbdtEblcMonochromeFont() const {\n'
                 '  return is_ebdt_eblc_;\n'
                 '}'),
                ('bool FontFormatCheck::IsColrCpalColorFontV0() const {\n'
                 '  return font_format::is_colrv0(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsColrCpalColorFontV0() const {\n'
                 '  return is_colrv0_;\n'
                 '}'),
                ('bool FontFormatCheck::IsColrCpalColorFontV1() const {\n'
                 '  return font_format::is_colrv1(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsColrCpalColorFontV1() const {\n'
                 '  return is_colrv1_;\n'
                 '}'),
                ('bool FontFormatCheck::IsSbixColorFont() const {\n'
                 '  return font_format::is_sbix(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsSbixColorFont() const { return is_sbix_; }'),
                ('bool FontFormatCheck::IsCff2OutlineFont() const {\n'
                 '  return font_format::is_cff2(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsCff2OutlineFont() const { return is_cff2_; }'),
                ('bool FontFormatCheck::IsAvar2Font() const {\n'
                 '  return RuntimeEnabledFeatures::FontFormatAvar2Enabled() &&\n'
                 '         font_format::is_avar2(format_info_);\n'
                 '}',
                 'bool FontFormatCheck::IsAvar2Font() const {\n'
                 '  return RuntimeEnabledFeatures::FontFormatAvar2Enabled() &&\n'
                 '         is_avar2_;\n'
                 '}'),
            ]),
        "third_party/blink/renderer/platform/instrumentation/BUILD.gn": (
            "65c31fe87a08cb59a36e8c1dabae98953a2b887e96bb7c644be33e6e344feef3", [
                ('    "partition_alloc_memory_dump_provider.cc",\n'
                 '    "partition_alloc_memory_dump_provider.h",\n', ''),
            ]),
        "third_party/blink/renderer/platform/fonts/font_cache.h": (
            "ce23838b223e37fc2a1e3933b3da5ed9478e07f0c9c2fef956ff28c95fa0248e", [
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 '  // These are needed for calling QueryRenderStyleForStrike, since',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 '  // These are needed for calling QueryRenderStyleForStrike, since'),
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 '  static float device_scale_factor_;',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 '  static float device_scale_factor_;'),
            ]),
        "third_party/blink/renderer/platform/fonts/font_cache.cc": (
            "2abe24f0e33a953222bc5dea8c2d43015124a2b87353216aa2cfd4092c4f7410", [
                ('#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 'float FontCache::device_scale_factor_ = 1.0;\n'
                 '#endif',
                 '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 'float FontCache::device_scale_factor_ = 1.0;\n'
                 '#endif'),
            ]),
        "third_party/blink/renderer/platform/graphics/video_frame_image_util.cc": (
            "8fa12b1af8eec05b7566dfa4e614036fd47a7865ecde176d3a6cdb4d18aa2e0c", [
                ('#include "gpu/config/gpu_feature_info.h"\n',
                 '#include "gpu/config/gpu_driver_bug_workaround_type.h"\n'
                 '#include "gpu/config/gpu_feature_info.h"\n'),
                ('          DISABLE_IMAGEBITMAP_FROM_VIDEO_USING_GPU)) {',
                 '          gpu::DISABLE_IMAGEBITMAP_FROM_VIDEO_USING_GPU)) {'),
            ]),
        "components/web_package/BUILD.gn": (
            "9079fc07340f27d68bc009740a1dafb44d38abafbac2b486e653d29721503209", [
                ('    "//components/cbor",\n'
                 '    "//components/web_package/signed_web_bundles/rust:signed_web_bundles_rust_bindings",\n'
                 '    "//mojo/public/cpp/bindings",',
                 '    "//components/cbor",\n'
                 '    "//mojo/public/cpp/bindings",'),
                ('  deps = [\n'
                 '    "//base",\n'
                 '    "//components/web_package/signed_web_bundles/rust:signed_web_bundles_rust_bindings",\n'
                 '    "//crypto",',
                 '  deps = [\n'
                 '    "//base",\n'
                 '    "//crypto",'),
            ]),
        "components/web_package/signed_web_bundles/ecdsa_p256_sha256_signature.cc": (
            "1a984d2e0b4b7492691fa3ee268c7f7f5ee95ffaf63383546d9e99ba9e603389", [
                ('#include "components/web_package/signed_web_bundles/rust/signed_web_bundles_rust.h"\n', ''),
                ('  return signed_web_bundles::rust::verify_ecdsa_p256_signature(\n'
                 '      public_key.bytes(), bytes(), message);',
                 '  // ECDSA verification lives in the removed Rust bridge.\n'
                 '  (void)message;\n'
                 '  (void)public_key;\n'
                 '  return false;'),
            ]),
        "components/web_package/signed_web_bundles/ed25519_signature.cc": (
            "aeb39f96a22787466decd7fa6e46155a9c1817d8b4fbd4c8b88df716f82130ae", [
                ('#include "components/web_package/signed_web_bundles/rust/signed_web_bundles_rust.h"\n', ''),
                ('  return signed_web_bundles::rust::verify_ed25519_signature(public_key.bytes(),\n'
                 '                                                            bytes(), message);',
                 '  return ED25519_verify(message.data(), message.size(), bytes().data(),\n'
                 '                        public_key.bytes().data()) == 1;'),
            ]),
        "components/web_package/signed_web_bundles/signed_web_bundle_utils.cc": (
            "c8f1a1c40544282921cb7ddb3aeb42608e20af399ce9acd11118e8b35c5712e3", [
                ('#include "components/web_package/signed_web_bundles/rust/signed_web_bundles_rust.h"\n', ''),
                ('std::vector<uint8_t> CreateSignaturePayload(\n'
                 '    const SignedWebBundleSignatureData& data) {\n'
                 '  auto payload_rust = signed_web_bundles::rust::create_signature_payload(\n'
                 '      data.unsigned_web_bundle_hash, data.integrity_block_cbor,\n'
                 '      data.attributes_cbor);\n'
                 '  return std::vector<uint8_t>(payload_rust.begin(), payload_rust.end());\n'
                 '}',
                 'namespace {\n'
                 '\n'
                 'void AppendPayloadItem(std::vector<uint8_t>* payload,\n'
                 '                       base::span<const uint8_t> item) {\n'
                 '  const uint64_t size = item.size();\n'
                 '  for (int shift = 56; shift >= 0; shift -= 8) {\n'
                 '    payload->push_back(static_cast<uint8_t>(size >> shift));\n'
                 '  }\n'
                 '  payload->insert(payload->end(), item.begin(), item.end());\n'
                 '}\n'
                 '\n'
                 '}  // namespace\n'
                 '\n'
                 'std::vector<uint8_t> CreateSignaturePayload(\n'
                 '    const SignedWebBundleSignatureData& data) {\n'
                 '  std::vector<uint8_t> payload;\n'
                 '  AppendPayloadItem(&payload, data.unsigned_web_bundle_hash);\n'
                 '  AppendPayloadItem(&payload, data.integrity_block_cbor);\n'
                 '  AppendPayloadItem(&payload, data.attributes_cbor);\n'
                 '  return payload;\n'
                 '}'),
            ]),
        "components/web_package/signed_web_bundles/integrity_block_parser.cc": (
            "ed043bdee9ce1656528efdfef109067d082791e2d97bc70a0e46d02bfe3f7a7c", [
                ('#include "components/web_package/signed_web_bundles/rust/signed_web_bundles_rust.h"\n', ''),
                ('using SignatureType = signed_web_bundles::rust::SignatureType::Tag;\n'
                 'using signed_web_bundles::rust::parse_integrity_block;\n'
                 '\n', ''),
                ('base::expected<mojom::BundleIntegrityBlockSignatureStackEntryPtr, std::string>\n'
                 'CreateSignatureStackEntry(\n'
                 '    const signed_web_bundles::rust::SignatureStackEntry& entry) {\n'
                 '  auto parsed_sig = mojom::BundleIntegrityBlockSignatureStackEntry::New();\n'
                 '\n'
                 '  switch (entry.signature_type.tag) {\n'
                 '    case SignatureType::Ed25519: {\n'
                 '      ASSIGN_OR_RETURN(auto public_key,\n'
                 '                       Ed25519PublicKey::Create(entry.public_key.to_span()));\n'
                 '      ASSIGN_OR_RETURN(auto signature,\n'
                 '                       Ed25519Signature::Create(entry.signature.to_span()));\n'
                 '      parsed_sig->signature_info = mojom::SignatureInfo::NewEd25519(\n'
                 '          mojom::SignatureInfoEd25519::New(public_key, signature));\n'
                 '      break;\n'
                 '    }\n'
                 '    case SignatureType::EcdsaP256SHA256: {\n'
                 '      ASSIGN_OR_RETURN(auto public_key,\n'
                 '                       EcdsaP256PublicKey::Create(entry.public_key.to_span()));\n'
                 '      ASSIGN_OR_RETURN(auto signature, EcdsaP256SHA256Signature::Create(\n'
                 '                                           entry.signature.to_span()));\n'
                 '      parsed_sig->signature_info = mojom::SignatureInfo::NewEcdsaP256Sha256(\n'
                 '          mojom::SignatureInfoEcdsaP256SHA256::New(public_key, signature));\n'
                 '      break;\n'
                 '    }\n'
                 '    case SignatureType::Unknown: {\n'
                 '      parsed_sig->signature_info =\n'
                 '          mojom::SignatureInfo::NewUnknown(mojom::SignatureInfoUnknown::New());\n'
                 '      break;\n'
                 '    }\n'
                 '  }\n'
                 '\n'
                 '  parsed_sig->attributes_cbor =\n'
                 '      BinaryData(entry.attributes_cbor.begin(), entry.attributes_cbor.end());\n'
                 '\n'
                 '  return parsed_sig;\n'
                 '}\n'
                 '\n', ''),
                ('  const auto parse_res = parse_integrity_block(*data);\n'
                 '  if (!parse_res.has_value()) {\n'
                 '    const auto& error = parse_res.error();\n'
                 '    RunErrorCallback(std::string(error.message.as_str()),\n'
                 '                     error.is_version_error\n'
                 '                         ? mojom::BundleParseErrorType::kVersionError\n'
                 '                         : mojom::BundleParseErrorType::kFormatError);\n'
                 '    return;\n'
                 '  }\n'
                 '\n'
                 '  const auto& parsed_ib = *parse_res;\n'
                 '  auto integrity_block = mojom::BundleIntegrityBlock::New();\n'
                 '  integrity_block->size = parsed_ib.size;\n'
                 '\n'
                 '  const std::string_view web_bundle_id_str =\n'
                 '      parsed_ib.web_bundle_id.to_string_view();\n'
                 '  RETURN_IF_ERROR(\n'
                 '      SignedWebBundleId::Create(web_bundle_id_str),\n'
                 '      [&](std::string error) { RunErrorCallback(std::move(error)); });\n'
                 '\n'
                 '  integrity_block->attributes =\n'
                 '      IntegrityBlockAttributes(std::string(web_bundle_id_str),\n'
                 '                               BinaryData(parsed_ib.attributes_cbor.begin(),\n'
                 '                                          parsed_ib.attributes_cbor.end()));\n'
                 '\n'
                 '  for (const auto& entry : parsed_ib.signature_stack) {\n'
                 '    ASSIGN_OR_RETURN(\n'
                 '        auto parsed_sig, CreateSignatureStackEntry(entry),\n'
                 '        [&](std::string error) { RunErrorCallback(std::move(error)); });\n'
                 '    integrity_block->signature_stack.push_back(std::move(parsed_sig));\n'
                 '  }\n'
                 '\n'
                 '  std::move(complete_callback_)\n'
                 '      .Run(base::BindOnce(std::move(result_callback_),\n'
                 '                          std::move(integrity_block), nullptr));',
                 '  // CharmOS excludes the Rust signed-web-bundle parser. Reject signed\n'
                 '  // bundles rather than accepting unverified content.\n'
                 '  RunErrorCallback("Signed Web Bundles are not supported on CharmOS.",\n'
                 '                   mojom::BundleParseErrorType::kFormatError);'),
            ]),
        "components/web_package/signed_web_bundles/signed_web_bundle_signature_verifier.cc": (
            "cfdd2e192130254c315461c28c62fedbbd770b38db592a3d0d6e7840999df1ae", [
                ('#include "components/web_package/signed_web_bundles/rust/signed_web_bundles_rust.h"\n', ''),
                ('  auto ib_cbor_rust =\n'
                 '      signed_web_bundles::rust::create_empty_integrity_block_cbor(\n'
                 '          integrity_block.attributes_cbor());\n'
                 '  return std::vector<uint8_t>(ib_cbor_rust.begin(), ib_cbor_rust.end());',
                 '  auto attributes_cbor = integrity_block.attributes_cbor();\n'
                 '  return std::vector<uint8_t>(attributes_cbor.begin(),\n'
                 '                              attributes_cbor.end());'),
                ('  signed_web_bundles::rust::Sha512Hasher hasher;\n',
                 '  SHA512_CTX hasher;\n  SHA512_Init(&hasher);\n'),
                ('    hasher.update(data);',
                 '    SHA512_Update(&hasher, data.data(), data.size());'),
                ('  SHA512Digest digest = std::move(hasher).finish();',
                 '  SHA512Digest digest;\n  SHA512_Final(digest.data(), &hasher);'),
            ]),
        "third_party/blink/renderer/core/dom/processing_instruction.cc": (
            "eb184b37094481de41498d244d938f037f5b8c1c2a502c34427a477d56941beb", [
                ('#include "third_party/blink/renderer/core/xml/parser/xml_document_parser_rs.h"  // for parseAttributesRust()\n', ''),
                ('  if (RuntimeEnabledFeatures::XMLParsingRustEnabled()) {\n'
                 '    attrs = blink::ParseAttributesRust(data_, attrs_ok);\n'
                 '  } else {\n'
                 '    attrs = blink::ParseAttributes(data_, attrs_ok);\n'
                 '  }',
                 '  attrs = blink::ParseAttributes(data_, attrs_ok);'),
            ]),
        "third_party/blink/renderer/core/execution_context/navigator_base.cc": (
            "64b29bb294db26511bd68cb304837bca69fd13b16daf318eae6efbe0565d944b", [
                ('#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 'constexpr char kLinuxX86_64[] = "Linux x86_64";',
                 '#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'
                 'constexpr char kLinuxX86_64[] = "Linux x86_64";'),
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
                 '  return kLinuxX86_64;',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 '  return kLinuxX86_64;'),
            ]),
        "third_party/blink/renderer/modules/media/audio/mojo_audio_input_ipc.cc": (
            "d32790ac818ee52c23cd3019089fa11eec81732e30c688473c17cef29ec55b9d", [
                ('                             std::move(socket_handle), initially_muted);',
                 '                             base::SyncSocket::ScopedHandle(),\n'
                 '                             initially_muted);'),
            ]),
        "third_party/blink/renderer/modules/media/audio/mojo_audio_output_ipc.cc": (
            "df3d47996cff0d94771a692d5d19a4184d61103b47ac17cb5756dece82012458", [
                ('                             std::move(socket_handle),',
                 '                             base::SyncSocket::ScopedHandle(),'),
                 ('  DCHECK(data_pipe->socket.is_valid_platform_file());\n',
                  '  // CharmOS has no audio output device yet: fail the stream instead of\n'
                  '  // aborting the renderer when a page (YouTube) starts playback.\n'
                  '  if (!data_pipe->socket.is_valid_platform_file()) {\n'
                  '    delegate_->OnError();\n'
                  '    return;\n'
                  '  }\n'),
            ]),
        "components/policy/core/common/cloud/cloud_policy_util.cc": (
            "792e95152fa99df555138805883521a71f948b45248f18e1ca1372661f193913", [
                ('#elif BUILDFLAG(IS_CHROMEOS)\n'
                 '  NOTREACHED();\n'
                 '#else\n'
                 '#error Unsupported platform\n'
                 '#endif',
                 '#elif BUILDFLAG(IS_CHROMEOS)\n'
                 '  NOTREACHED();\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return std::string();\n'
                 '#else\n'
                 '#error Unsupported platform\n'
                 '#endif'),
            ]),
        "components/trusted_vault/trusted_vault_connection_impl.cc": (
            "e6572fd25aecdcceb963746ce9f92cba3b8d5a2a88b7e2f06605086f9b4f55aa", [
                ('#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '  // Not used in Fuchsia.\n'
                 '  return trusted_vault_pb::PhysicalDeviceMetadata::DEVICE_TYPE_UNKNOWN;\n'
                 '#else\n'
                 '#error Please handle your new device OS here.\n'
                 '#endif',
                 '#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '  // Not used in Fuchsia.\n'
                 '  return trusted_vault_pb::PhysicalDeviceMetadata::DEVICE_TYPE_UNKNOWN;\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return trusted_vault_pb::PhysicalDeviceMetadata::DEVICE_TYPE_UNKNOWN;\n'
                 '#else\n'
                 '#error Please handle your new device OS here.\n'
                 '#endif'),
            ]),
        "components/update_client/update_query_params.cc": (
            "58ff124c2ff76af8b412c599bb79ec22db02af3a80acda69e1fb271fc4f2b896", [
                ('#elif BUILDFLAG(IS_LINUX)\n    "linux";',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHARMOS)\n    "linux";'),
            ]),
        "components/webui/flags/flags_state.cc": (
            "64aa06d9e6dfc9fa6b66460b47339f59e0382cab0273d4f236692fe9e8f284b6", [
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_OPENBSD)\n'
                 '  return kOsLinux;',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_OPENBSD) || '
                 'BUILDFLAG(IS_CHARMOS)\n'
                 '  return kOsLinux;'),
            ]),
        "services/data_decoder/public/cpp/xml_dom_parser.cc": (
            "93b2dc06ae95435ee1b7687ab1b4da8d5e36b51d6f2c43d6d7d9212e225f6ed4", [
                ('#include "base/containers/span_rust.h"\n'
                 '#include "base/strings/string_view_rust.h"\n'
                 '#include "services/data_decoder/public/cpp/xml_dom.h"\n'
                 '#include "services/data_decoder/xml/cxx.rs.h"\n',
                 '#include "services/data_decoder/public/cpp/xml_dom.h"\n'),
                ('base::expected<Document, std::string> Document::FromBytes(\n'
                 '    base::span<const uint8_t> bytes) {\n'
                 '  std::string err;\n'
                 '  auto root = ffi::decode_xml_bytes(base::SpanToRustSlice(bytes), err);\n'
                 '  if (root) {\n'
                 '    return Document(std::move(root));\n'
                 '  }\n'
                 '  return base::unexpected(err);\n'
                 '}',
                 'base::expected<Document, std::string> Document::FromBytes(\n'
                 '    base::span<const uint8_t> bytes) {\n'
                 '  (void)bytes;\n'
                 '  return base::unexpected(\n'
                 '      "XML DOM decoding is not supported on CharmOS.");\n'
                 '}'),
                ('base::expected<Document, std::string> Document::FromUtf8(std::string_view str) {\n'
                 '  std::string err;\n'
                 '  auto root = ffi::decode_xml_str(base::StringViewToRustStrUTF8(str), err);\n'
                 '  if (root) {\n'
                 '    return Document(std::move(root));\n'
                 '  }\n'
                 '  return base::unexpected(err);\n'
                 '}',
                 'base::expected<Document, std::string> Document::FromUtf8(\n'
                 '    std::string_view str) {\n'
                 '  (void)str;\n'
                 '  return base::unexpected(\n'
                 '      "XML DOM decoding is not supported on CharmOS.");\n'
                 '}'),
            ]),
        "content/common/font_list_fontconfig.cc": (
            "72e400c77600d5b8d500e1a75d883b22d2953da78c29d9feffc260e1b57a5b97", [
                ('#include <fontconfig/fontconfig.h>\n',
                 '#include "build/build_config.h"\n'),
                ('std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> CreateFormatPattern(',
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 'std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> CreateFormatPattern('),
                ('  return font_list;\n'
                 '}\n'
                 '\n'
                 '}  // namespace content',
                 '  return font_list;\n'
                 '}\n'
                 '#else\n'
                 'base::ListValue GetFontList_SlowBlocking() {\n'
                 '  base::ListValue font_list;\n'
                 '  for (const char* family : {"Monospace", "Sans", "Serif"}) {\n'
                 '    base::ListValue font_item;\n'
                 '    font_item.Append(family);\n'
                 '    font_item.Append(family);\n'
                 '    font_list.Append(std::move(font_item));\n'
                 '  }\n'
                 '  return font_list;\n'
                 '}\n'
                 '#endif\n'
                 '\n'
                 '}  // namespace content'),
            ]),
        "content/renderer/webgraphicscontext3d_provider_impl.cc": (
            "862b423a3f7ae950ae9a16eb0c23bba4383b369018c585788a8370a3bfef3b04", [
                ('    if (gpu_feature_info.IsWorkaroundEnabled(MAX_MSAA_SAMPLE_COUNT_2))',
                 '    if (gpu_feature_info.IsWorkaroundEnabled(MAX_MSAA_SAMPLE_COUNT_4))'),
            ]),
        "services/audio/public/cpp/input_ipc.cc": (
            "a3c86539967d86834610c393a68fa6e1113ad40cf1175d7097a537cb2cccc3e2", [
                ('  delegate_->OnStreamCreated(std::move(shared_memory_region),\n'
                 '                             std::move(socket_handle), initially_muted);',
                 '  delegate_->OnStreamCreated(std::move(shared_memory_region),\n'
                 '                             base::SyncSocket::ScopedHandle(),\n'
                 '                             initially_muted);'),
            ]),
        "services/cert_verifier/cert_verifier_creation.cc": (
            "96da50c702dc05db70d8bf256e112e51a044357fbb9078207db74d8b3e76165f", [
                ('#if BUILDFLAG(IS_CHROMEOS)\n'
                 '    trust_store = net::CreateChromeOnlySystemTrustStore(std::move(chrome_root));\n'
                 '#else\n',
                 '#if BUILDFLAG(IS_CHROMEOS)\n'
                 '    trust_store = net::CreateChromeOnlySystemTrustStore(std::move(chrome_root));\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '    trust_store = net::CreateSslSystemTrustStore();\n'
                 '#else\n'),
            ]),
        "services/cert_verifier/cert_verifier_service_factory.cc": (
            "876893c412de6ea00bad098201fb1b94b19fc426cabb250eaeb7a858ccb9510c", [
                ('  std::unique_ptr<net::SystemTrustStore> system_trust_store =\n'
                 '      net::CreateSslSystemTrustStoreChromeRoot(\n'
                 '          std::make_unique<net::TrustStoreChrome>());',
                 '#if BUILDFLAG(IS_CHARMOS)\n'
                 '  std::unique_ptr<net::SystemTrustStore> system_trust_store =\n'
                 '      net::CreateSslSystemTrustStore();\n'
                 '#else\n'
                 '  std::unique_ptr<net::SystemTrustStore> system_trust_store =\n'
                 '      net::CreateSslSystemTrustStoreChromeRoot(\n'
                 '          std::make_unique<net::TrustStoreChrome>());\n'
                 '#endif'),
            ]),
        "services/audio/public/cpp/output_device.cc": (
            "14163ef64473ca0c6859668c44ebd4be41b0a22e2dd1d9935c49e111bbf4304b", [
                ('      audio_callback_.get(), std::move(socket_handle), "audio::OutputDevice",',
                 '      audio_callback_.get(), base::SyncSocket::ScopedHandle(),\n'
                 '      "audio::OutputDevice",'),
            ]),
        "components/safe_browsing/content/resources/BUILD.gn": (
            "e99503fa6d78fc547d5b1e8f0b7be1437c2d84979da6ed4d84be581dff3a70d8", [
                ('  } else if (is_linux) {\n    target_arch = "linux"',
                 '  } else if (is_linux || is_charmos) {\n    target_arch = "linux"'),
            ]),
        "tools/protoc_wrapper/protoc_wrapper.py": (
            "6c36a160c096eaf9006ccdc410d2f53a28eaea4bd2a6b4e77670925ce27e409f", [
                ('  if options.fatal_warnings:\n    protoc_cmd += ["--fatal_warnings"]\n',
                 '  # CharmOS: protoc warnings (for example unused imports) are not fatal.\n'),
            ]),
        "components/policy/tools/generate_policy_source.gni": (
            "f01568f435a6567fccbb544e87dd452ad04d5c8c50f8d375691f3a0a8300e51f", [
                ('  action(target_name) {\n',
                 '  if (is_charmos) {\n'
                 '    policy_target_platform = "linux"\n'
                 '  } else {\n'
                 '    policy_target_platform = target_os\n'
                 '  }\n'
                 '\n'
                 '  action(target_name) {\n'),
                ('      "--target-platform=" + target_os,',
                 '      "--target-platform=" + policy_target_platform,'),
            ]),
        "components/feature_engagement/public/feature_constants.h": (
            "fbfba1f0d702fd9f8bac1f4e4e2706c81c41f077e687eb549e9e5b8213515c3f", [
                ('#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)\n'
                 'FEATURE_CONSTANTS_DECLARE_FEATURE(kIPHAutofillAtMemoryFeature);',
                 '#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || '
                 'BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'
                 'FEATURE_CONSTANTS_DECLARE_FEATURE(kIPHAutofillAtMemoryFeature);'),
            ]),
        "components/feature_engagement/public/feature_constants.cc": (
            "aef339cebd8ecf19a7f75c2559413cfcf2b7a28f809344eea9fea41743059cfc", [
                ('#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)\n',
                 '#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n'
                 '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || '
                 'BUILDFLAG(IS_FUCHSIA) || \\\n'
                 '    BUILDFLAG(IS_CHARMOS)\n'),
            ]),
        "components/content_settings/core/browser/website_settings_registry.cc": (
            "7a475b1d2e2e13562f373d9ab5ff9b9c67c6b577fbe2058b04520d393c83adbe", [
                ('#elif BUILDFLAG(IS_LINUX)\n'
                 '  if (!(platform & PLATFORM_LINUX))',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHARMOS)\n'
                 '  if (!(platform & PLATFORM_LINUX))'),
            ]),
        "components/embedder_support/user_agent_utils.cc": (
            "21d06edd8f0b76a0bfef8c7aab2448e4f046f788e24da5591713c3561ea54708", [
                ('#elif BUILDFLAG(IS_IOS)\n'
                 '  return ui::GetDeviceFormFactor() == ui::DEVICE_FORM_FACTOR_TABLET\n'
                 '             ? "iPad; "\n'
                 '             : "iPhone; ";\n'
                 '#else\n'
                 '#error Unsupported platform\n'
                 '#endif',
                 '#elif BUILDFLAG(IS_IOS)\n'
                 '  return ui::GetDeviceFormFactor() == ui::DEVICE_FORM_FACTOR_TABLET\n'
                 '             ? "iPad; "\n'
                 '             : "iPhone; ";\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return "X11; Linux x86_64";\n'
                 '#else\n'
                 '#error Unsupported platform\n'
                 '#endif'),
                ('#elif BUILDFLAG(IS_LINUX)\n'
                 '  return "X11; Linux x86_64";',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHARMOS)\n'
                 '  return "X11; Linux x86_64";'),
            ]),
        "components/sync_device_info/local_device_info_util.cc": (
            "f94d5ee21002bcdd3a1ec77f73c8a52f4eae95760f3cf56ff2d0065cbf9b1fa3", [
                ('#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '  return DeviceInfo::OsType::kFuchsia;\n'
                 '#else\n'
                 '#error Please handle your new device OS here.\n'
                 '#endif',
                 '#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '  return DeviceInfo::OsType::kFuchsia;\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  return DeviceInfo::OsType::kLinux;\n'
                 '#else\n'
                 '#error Please handle your new device OS here.\n'
                 '#endif'),
            ]),
        "components/variations/service/variations_service.cc": (
            "9a5abaf960b6465451fc0f17f806f1078447fee555e8baa020a73c14f503b7eb", [
                ('#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS)',
                 '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS) || '
                 'BUILDFLAG(IS_CHARMOS)'),
            ]),
        "components/translate/core/browser/translate_language_list.cc": (
            "192caec2c15b5324c9c02b9a61b504612b8d55e8cf00f3a6a02724f7bab1b5b0", [
                ('#include <optional>\n', '#include <optional>\n#include <set>\n'),
                ('constexpr auto kDefaultSupportedPartialTranslateLanguages =',
                 '#pragma clang diagnostic push\n'
                 '#pragma clang diagnostic ignored "-Wexit-time-destructors"\n'
                 'const auto kDefaultSupportedPartialTranslateLanguages ='),
                ('    base::MakeFixedFlatSet<LanguageTag>({',
                 '    std::set<LanguageTag>({'),
                ('        GetKnownLanguageTag("zu"),     // Zulu\n'
                 '    });',
                 '        GetKnownLanguageTag("zu"),     // Zulu\n'
                 '    });\n'
                 '#pragma clang diagnostic pop'),
            ]),
        "content/app/content_main_runner_impl.cc": (
            "742c7e6d80584e4aa172771615a5602063aab66fed8254d11375f55db5fb49d0", [
                ('  delegate->ZygoteStarting(&zygote_fork_delegates);',
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '  delegate->ZygoteStarting(&zygote_fork_delegates);\n'
                 '#endif'),
                ('  delegate->ZygoteForked();',
                 '#if !BUILDFLAG(IS_CHARMOS)\n  delegate->ZygoteForked();\n#endif'),
            ]),
        "content/browser/devtools/protocol/media_recorder.cc": (
            "bdd28b29ad339f8bcbdb0a9454416bc75178cfd719dae9eec3f9119b1ca76075", [
                ('    delegate_->OnStreamCreated(std::move(data_pipe->shared_memory),\n'
                 '                               std::move(socket_handle), false);',
                 '    delegate_->OnStreamCreated(std::move(data_pipe->shared_memory),\n'
                 '                               base::SyncSocket::ScopedHandle(), false);'),
            ]),
        "content/browser/video_capture_service_impl.cc": (
            "fe26d1f908494795feef87e694af03247667e8fbf8ab6be174601f2239071d44", [
                ('#include "content/browser/gpu/browser_gpu_channel_host_factory.h"\n',
                 '#include "content/browser/gpu/browser_gpu_channel_host_factory.h"\n'
                 '#include "gpu/ipc/client/gpu_channel_host.h"\n'),
            ]),
        "content/browser/service_host/utility_sandbox_delegate.cc": (
            "b219aa8efea057702744573ccc2b9ff044c70cc46b16ae7929f9708cc631e42e", [
                ('#include "content/public/common/zygote/zygote_buildflags.h"\n'
                 '#include "media/gpu/buildflags.h"\n'
                 '#include "sandbox/policy/mojom/sandbox.mojom.h"\n',
                 '#include "content/public/common/zygote/zygote_buildflags.h"\n'
                 '#include "media/gpu/buildflags.h"\n'
                 '#include "media/media_buildflags.h"\n'
                 '#include "sandbox/policy/mojom/sandbox.mojom.h"\n'),
            ]),
        "content/shell/common/shell_paths.cc": (
            "d19225ef1c8017cf96466945cf026d5b71da62ef3f78266631d019789a776383", [
                ('#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '  *result = base::FilePath(base::kPersistedDataDirectoryPath)\n'
                 '                .Append(FILE_PATH_LITERAL("content_shell"));\n'
                 '#else',
                 '#elif BUILDFLAG(IS_FUCHSIA)\n'
                 '  *result = base::FilePath(base::kPersistedDataDirectoryPath)\n'
                 '                .Append(FILE_PATH_LITERAL("content_shell"));\n'
                 '#elif BUILDFLAG(IS_CHARMOS)\n'
                 '  // Content shell data lives under the native app data root.\n'
                 '  *result = base::FilePath("/data/content_shell");\n'
                 '#else'),
            ]),
        "content/shell/browser/shell_content_browser_client.cc": (
            "06d8704628ca5b7672dc93ec8bba079c679ef2c305f21ed84222e77511cd6e95", [
                ('#include "content/shell/browser/rust_test_service_ffi.rs.h"\n', ''),
                ('#include "content/shell/common/rust_test.test-mojom.h"\n', ''),
                ('#include "base/feature_list.h"\n',
                 '#include "base/feature_list.h"\n'
                 '#include "base/metrics/field_trial.h"\n'),
                ('void BindRustTestServiceReceiver(\n'
                 '    RenderFrameHost* render_frame_host,\n'
                 '    mojo::PendingReceiver<content::rust_test::mojom::RustTestService>\n'
                 '        receiver) {\n'
                 '  auto pipe = std::make_unique<mojo::rust::ScopedMessagePipeHandleWrapper>(\n'
                 '      receiver.PassPipe());\n'
                 '  content::rust_test::BindRustTestService(std::move(pipe));\n'
                 '}\n'
                 '\n', ''),
                ('  map->Add<content::rust_test::mojom::RustTestService>(\n'
                 '      base::BindRepeating(&BindRustTestServiceReceiver));\n', ''),
                ('void ShellContentBrowserClient::CreateFeatureListAndFieldTrials() {\n'
                 '  GetSharedState().local_state = CreateLocalState();\n'
                 '  SetupFieldTrials();\n'
                 '}',
                 'void ShellContentBrowserClient::CreateFeatureListAndFieldTrials() {\n'
                 '  GetSharedState().local_state = CreateLocalState();\n'
                 '#if BUILDFLAG(IS_CHARMOS)\n'
                 '  // Mojo requires a FeatureList before InitializeMojoCore() and the\n'
                 '  // browser registers a FieldTrial observer before any study is\n'
                 '  // parsed. The full field-trial pipeline depends on services CharmOS\n'
                 '  // does not host, so install just the required empty singletons.\n'
                 '  static base::NoDestructor<base::FieldTrialList> field_trial_list;\n'
                 '  base::FeatureList::InitInstance(std::string(), std::string());\n'
                 '#else\n'
                 '  SetupFieldTrials();\n'
                 '#endif\n'
                 '}'),
                ('bool ShellContentBrowserClient::HasCustomSchemeHandler(\n'
                 '    content::BrowserContext* browser_context,\n'
                 '    const std::string& scheme) {\n'
                 '  if (custom_handlers::ProtocolHandlerRegistry* protocol_handler_registry =',
                 'bool ShellContentBrowserClient::HasCustomSchemeHandler(\n'
                 '    content::BrowserContext* browser_context,\n'
                 '    const std::string& scheme) {\n'
                 '#if BUILDFLAG(IS_CHARMOS)\n'
                 '  (void)browser_context;\n'
                 '  (void)scheme;\n'
                 '  return false;\n'
                 '#else\n'
                 '  if (custom_handlers::ProtocolHandlerRegistry* protocol_handler_registry ='),
                ('    return protocol_handler_registry->IsHandledProtocol(scheme);\n'
                 '  }\n'
                 '  return false;\n'
                 '}',
                 '    return protocol_handler_registry->IsHandledProtocol(scheme);\n'
                 '  }\n'
                 '  return false;\n'
                 '#endif\n'
                 '}'),
                ('  auto* factory = custom_handlers::SimpleProtocolHandlerRegistryFactory::\n'
                 '      GetForBrowserContext(browser_context);\n'
                 '  // null in unit tests.\n'
                 '  if (factory) {\n'
                 '    result.push_back(\n'
                 '        std::make_unique<custom_handlers::ProtocolHandlerThrottle>(*factory));\n'
                 '  }',
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '  auto* factory = custom_handlers::SimpleProtocolHandlerRegistryFactory::\n'
                 '      GetForBrowserContext(browser_context);\n'
                 '  // null in unit tests.\n'
                 '  if (factory) {\n'
                 '    result.push_back(\n'
                 '        std::make_unique<custom_handlers::ProtocolHandlerThrottle>(*factory));\n'
                 '  }\n'
                 '#else\n'
                 '  (void)browser_context;\n'
                 '#endif'),
            ]),
        "content/browser/security/cpsp/child_process_security_policy_impl.cc": (
            "af17f82cd35e27272fdf63cc9cea49a47b6937d27fd9d279c0b29d8016ad6a23", [
                ('#include "content/browser/security/cpsp/child_process_security_policy_impl.rs.h"\n', ''),
                ('#include "content/browser/security/cpsp/process_state.rs.h"\n', ''),
                ('#include "content/browser/origin_agent_cluster_isolation_state_bridge.h"\n', ''),
                ('// Used internally only. These bit positions have no relationship to any\n'
                 '// underlying OS and can be changed to accommodate finer-grained permissions.\n',
                 '#undef RUST_CPP_VOID_FUNCTION\n'
                 '#undef RUST_CPP_RETURN_FUNCTION\n'
                 '#undef RUST_CPP_PROCESS_STATE_VOID_FUNCTION\n'
                 '#undef RUST_CPP_PROCESS_STATE_RETURN_FUNCTION\n'
                 '// CharmOS is Rust-free: always execute the C++ implementation.\n'
                 '#define RUST_CPP_VOID_FUNCTION(rust_expr, cpp_expr, ...) cpp_expr\n'
                 '#define RUST_CPP_RETURN_FUNCTION(rust_expr, cpp_expr, ...) return cpp_expr\n'
                 '#define RUST_CPP_PROCESS_STATE_VOID_FUNCTION(rust_expr, cpp_expr) cpp_expr\n'
                 '#define RUST_CPP_PROCESS_STATE_RETURN_FUNCTION(rust_expr, cpp_expr) return cpp_expr\n'
                 '\n'
                 '// Used internally only. These bit positions have no relationship to any\n'
                 '// underlying OS and can be changed to accommodate finer-grained permissions.\n'),
                ('  rust::child_process_security_policy::\n'
                 '      clear_all_registered_schemes_for_testing();\n'
                 '\n', ''),
                ('  if (IsRustEnabled(GetRustPolicy(CpspRustFeature::kProcessState))) {\n'
                 '    rust::child_process_security_policy::create_state_for_process(child_id);\n'
                 '  }\n', ''),
                ('  if (IsRustEnabled(GetRustPolicy(CpspRustFeature::kProcessState))) {\n'
                 '    rust::child_process_security_policy::prepare_to_remove_state(child_id);\n'
                 '  }\n', ''),
                ('  if (IsRustEnabled(rust_cpp_policy)) {\n'
                 '    auto rust_match = rust::child_process_security_policy::\n'
                 '        get_matching_process_isolated_origin_from_legacy_origin_list(\n'
                 '            browser_context_id, browsing_instance_id,\n'
                 '            // Make copies of `origin` and `site_url` for Rust to own.\n'
                 '            std::make_unique<url::Origin>(origin),\n'
                 '            std::make_unique<GURL>(site_url));\n'
                 '    if (rust_match) {\n'
                 '      rust_result = std::make_optional(*rust_match);\n'
                 '    }\n'
                 '  }\n', ''),
                ('  if (IsRustEnabled(policy)) {\n'
                 '    rust::child_process_security_policy::OriginAgentClusterIsolationState\n'
                 '        state = rust::child_process_security_policy::\n'
                 '            OriginAgentClusterIsolationState::SiteKeyedByDefault;\n'
                 '    if (rust::child_process_security_policy::lookup_origin_agent_cluster_state(\n'
                 '            // Make a copy of the origin for Rust to own.\n'
                 '            browsing_instance_id, std::make_unique<url::Origin>(origin),\n'
                 '            state)) {\n'
                 '      rust_result =\n'
                 '          std::make_optional(FromRustOriginAgentClusterIsolationState(state));\n'
                 '    }\n'
                 '  }\n', ''),
                ('  if (IsRustEnabled(policy)) {\n'
                 '    bool result = false;\n'
                 '    if (rust::child_process_security_policy::\n'
                 '            lookup_are_v8_optimizations_disabled(\n'
                 '                browsing_instance_id,\n'
                 '                // Make a copy for Rust to own.\n'
                 '                std::make_unique<url::Origin>(process_lock_origin), result)) {\n'
                 '      rust_result = std::make_optional(result);\n'
                 '    }\n'
                 '  }\n', ''),
                ('  if (IsRustEnabled(GetRustPolicy(CpspRustFeature::kProcessState))) {\n'
                 '    rust::child_process_security_policy::complete_pending_state_removal(\n'
                 '        child_id);\n'
                 '  }\n', ''),
            ]),
        "content/browser/BUILD.gn": (
            "0281cdd6eb08b8ac97423aff9070131d24a0959bda072ef593f2ebbf301f0487", [
                ('    "origin_agent_cluster_isolation_state_bridge.cc",\n'
                 '    "origin_agent_cluster_isolation_state_bridge.h",\n', ''),
                ('  if (is_win || is_apple || is_linux || is_chromeos || is_desktop_android ||\n'
                 '      is_fuchsia) {\n'
                 '    sources += [\n'
                 '      "service_worker/service_worker_usb_delegate_observer.cc",',
                 '  if (is_win || is_apple || is_linux || is_chromeos || is_desktop_android ||\n'
                 '      is_fuchsia || is_charmos) {\n'
                 '    sources += [\n'
                 '      "service_worker/service_worker_usb_delegate_observer.cc",'),
            ]),
        "content/public/common/features.gni": (
            "a2519bb334d90af62a7b67e297cb1922e2d2720f9389886984c09e5a803bd70c", [
                ('enable_screen_capture = is_linux || is_chromeos || (is_apple && use_blink) ||\n'
                 '                        is_win || is_android || is_fuchsia',
                 'enable_screen_capture = is_linux || is_chromeos || (is_apple && use_blink) ||\n'
                 '                        is_win || is_android || is_fuchsia || is_charmos'),
            ]),
        "content/shell/browser/shell_browser_main_parts.h": (
            "db577e2141dc3660ee5980006e0d9963ae0779034f881ef6a9b98f2e801b596b", [
                ('  ShellBrowserContext* off_the_record_browser_context() {\n    return off_the_record_browser_context_.get();\n  }', '  ShellBrowserContext* off_the_record_browser_context();'),
            ]),
        "content/shell/browser/shell_browser_main_parts.cc": (
            "5d4282dd42e07cd46b132f00faa78f3f28a667c76441ffedc9ae3791207b8687", [
                ('#include "content/shell/browser/shell_browser_main_parts.h"', '#include "content/shell/browser/shell_browser_main_parts.h"\n#include "build/charmos/startup_profile_charmos.h"'),
                ('int ShellBrowserMainParts::PreEarlyInitialization() {', 'int ShellBrowserMainParts::PreEarlyInitialization() {\n  CharmStartupStage("PreEarlyInitialization");'),
                ('int ShellBrowserMainParts::PreCreateThreads() {', 'int ShellBrowserMainParts::PreCreateThreads() {\n  CharmStartupStage("PreCreateThreads");'),
                ('int ShellBrowserMainParts::PostCreateThreads() {', 'int ShellBrowserMainParts::PostCreateThreads() {\n  CharmStartupStage("PostCreateThreads");'),
                ('int ShellBrowserMainParts::PreMainMessageLoopRun() {', 'int ShellBrowserMainParts::PreMainMessageLoopRun() {\n  CharmStartupStage("PreMainMessageLoopRun");'),
                ('void ShellBrowserMainParts::PostCreateMainMessageLoop() {', 'void ShellBrowserMainParts::PostCreateMainMessageLoop() {\n  CharmStartupStage("PostCreateMainMessageLoop");'),
                ('void ShellBrowserMainParts::InitializeBrowserContexts() {', 'void ShellBrowserMainParts::InitializeBrowserContexts() {\n  CharmStartupStage("InitializeBrowserContexts");'),
                ('void ShellBrowserMainParts::InitializeMessageLoopContext() {', 'void ShellBrowserMainParts::InitializeMessageLoopContext() {\n  CharmStartupStage("InitializeMessageLoopContext");'),
                ('void ShellBrowserMainParts::ToolkitInitialized() {', 'void ShellBrowserMainParts::ToolkitInitialized() {\n  CharmStartupStage("ToolkitInitialized");'),
                ('  set_browser_context(new ShellBrowserContext(false));', '  set_browser_context(new ShellBrowserContext(false));\n  CharmStartupStage("context-normal-done");'),
                ('  set_off_the_record_browser_context(new ShellBrowserContext(true));', '  set_off_the_record_browser_context(new ShellBrowserContext(true));\n  CharmStartupStage("context-private-done");'),
                ('  browser_context_->GetOriginTrialsControllerDelegate();', '  browser_context_->GetOriginTrialsControllerDelegate();\n  CharmStartupStage("trials-normal-done");'),
                ('  off_the_record_browser_context_->GetOriginTrialsControllerDelegate();', '  off_the_record_browser_context_->GetOriginTrialsControllerDelegate();\n  CharmStartupStage("trials-private-done");'),
                ('  off_the_record_browser_context_->GetOriginTrialsControllerDelegate();\n  CharmStartupStage("trials-private-done");', '#if !BUILDFLAG(IS_CHARMOS)\n  off_the_record_browser_context_->GetOriginTrialsControllerDelegate();\n#endif\n  CharmStartupStage("private-context-prepared");'),
                ('void ShellBrowserMainParts::InitializeBrowserContexts() {', 'ShellBrowserContext* ShellBrowserMainParts::off_the_record_browser_context() {\n#if BUILDFLAG(IS_CHARMOS)\n  // Prepare private-profile services before its first consumer, rather than\n  // opening a second unused StoragePartition during a normal-window launch.\n  if (off_the_record_browser_context_)\n    off_the_record_browser_context_->GetOriginTrialsControllerDelegate();\n#endif\n  return off_the_record_browser_context_.get();\n}\n\nvoid ShellBrowserMainParts::InitializeBrowserContexts() {'),
            ]),
        "content/shell/app/shell_main.cc": (
            "e5a969a5ceb315b6dfe666fb85f3c4d4f74991540ecbd2cc391dc41cae0097ba", [
                ('int main(int argc, const char** argv) {\n'
                 '  content::ShellMainDelegate delegate;',
                 'extern "C" int main(int argc, const char** argv) {\n'
                 '  content::ShellMainDelegate delegate;'),
                ('#include "build/build_config.h"', '#include "build/build_config.h"\n#include "build/charmos/startup_profile_charmos.h"'),
                ('extern "C" int main(int argc, const char** argv) {', 'extern "C" int main(int argc, const char** argv) {\n  CharmStartupStage("main");'),
            ]),
        "base/allocator/partition_allocator/src/partition_alloc/partition_alloc_base/time/time_now_posix.cc": (
            "11b3295826f410c12400ecd6f11e235c3670e1520360cbc90c43689ff6608cb4", [
                ('#include <unistd.h>\n',
                 '#include <unistd.h>\n'
                 '\n'
                 '// CharmOS provides clock_gettime(CLOCK_MONOTONIC); expose the POSIX\n'
                 '// feature macro used below.\n'
                 '#ifndef _POSIX_MONOTONIC_CLOCK\n'
                 '#define _POSIX_MONOTONIC_CLOCK 200809L\n'
                 '#endif\n'),
                ('  struct timezone tz = {0, 0};  // UTC\n'
                 '  PA_BASE_CHECK(gettimeofday(&tv, &tz) == 0);',
                 '  PA_BASE_CHECK(gettimeofday(&tv, nullptr) == 0);'),
            ]),
        "third_party/blink/renderer/modules/BUILD.gn": (
            "4541baee221f41566b080e2def9ed5ed0abc297c1f379423ea19cd16e141292e", [
                ('    "//third_party/blink/renderer/modules/webgl",\n'
                 '    "//third_party/blink/renderer/modules/webgpu",\n'
                 '    "//third_party/blink/renderer/modules/webmidi",',
                 '    "//third_party/blink/renderer/modules/webgl",\n'
                 '    "//third_party/blink/renderer/modules/webmidi",'),
            ]),
        "components/viz/common/BUILD.gn": (
            "5bd7fa45799b5f0115bfa0a4aaacf0192208edfcd0f6c3573fcc721e44344ceb", [
                ('    "//ui/gfx/geometry:geometry_skia",\n    "//ui/gl",\n    "//ui/latency",',
                 '    "//ui/gfx/geometry:geometry_skia",\n    "//ui/latency",'),
            ]),
        "third_party/blink/renderer/modules/canvas/BUILD.gn": (
            "227c1e8db4cbba829d464595e5d4adea9103f2ee50fa2e5b54e2429e77a4e8de", [
                ('    "//third_party/blink/renderer/modules/webgpu",\n', ''),
            ]),
        "gpu/config/BUILD.gn": (
            "a4e840f4ff5892fe61d35b8ba5639384bcbe0ccf92b957f329646dd380e36f5c", [
                ('    "//ui/gfx",\n'
                 '    "//ui/gl",\n'
                 '    "//ui/gl:buildflags",\n'
                 '    "//ui/gl:gl_headers",\n'
                 '    "//ui/gl/init",\n'
                 '  ]',
                 '    "//ui/gfx",\n'
                 '  ]'),
            ]),
        "third_party/angle/src/common/platform.h": (
            "6c839dfbe39dfb6453b4cd8a0f5151d9c8d50be9530151f6562a6a9b3de57d88", [
                ('#elif defined(__linux__) || defined(EMSCRIPTEN)',
                 '#elif defined(__linux__) || defined(EMSCRIPTEN) || defined(__charmos__)'),
            ]),
        "third_party/dawn/src/utils/platform.h": (
            "28c73c24cc45e33b52aedba8deb9be6743be61237feedfdcdffe97fea664f28c", [
                ('#elif defined(__linux__)\n#define DAWN_PLATFORM_IS_LINUX 1',
                 '#elif defined(__linux__) || defined(__charmos__)\n#define DAWN_PLATFORM_IS_LINUX 1'),
            ]),
        "third_party/webrtc/rtc_base/platform_thread_types.cc": (
            "c3f57298b272df5a1e130f0f7e29efe1df8109c50eab642f4066b2639f77b784", [
                ('#elif defined(WEBRTC_LINUX)\n'
                 '  return syscall(__NR_gettid);\n'
                 '#elif defined(__EMSCRIPTEN__)',
                 '#elif defined(WEBRTC_LINUX)\n'
                 '  return syscall(__NR_gettid);\n'
                 '#elif defined(__charmos__)\n'
                 '  return static_cast<PlatformThreadId>(gettid());\n'
                 '#elif defined(__EMSCRIPTEN__)'),
            ]),
        "third_party/libsrtp/crypto/cipher/cipher.c": (
            "25e4d796f872f5032486f833a842262864196ed907df06daaad126182f465909", [
                ('#include <config.h>\n', '#include <config.h>\n#include <time.h>\n'),
            ]),
        "third_party/ffmpeg/ffmpeg_options.gni": (
            "5e4192356f0efac34f8ebfa0e789bc5a862ddf631acf22e57e3455de9876a736", [
                ('os_config = current_os\n',
                 'os_config = current_os\n'
                 'if (is_charmos) {\n'
                 '  # CharmOS is POSIX-like and uses the checked-in Linux C tables.\n'
                 '  os_config = "linux"\n'
                 '}\n'),
            ]),
        "third_party/blink/renderer/platform/image-decoders/BUILD.gn": (
            "6c5e4f17f3f0f43797679de7f337ab99ef6d08f8da27b72ba25bd24494e06bcb", [
                ('  if (enable_dav1d_decoder) {\n'
                 '    sources += [\n'
                 '      "avif/avif_image_decoder.cc",',
                 '  if (enable_dav1d_decoder && !is_charmos) {\n'
                 '    sources += [\n'
                 '      "avif/avif_image_decoder.cc",'),
            ]),
        "third_party/blink/common/BUILD.gn": (
            "e8d6c078a72041cc615f52b613ae94dca8361604df3a44cd4f7385bb03ac2752", [
                ('    "//third_party/blink/common/rust_crash",\n', ''),
            ]),
        "components/base32/BUILD.gn": (
            "93cd1c37b27045bcd7a059c4d36254462e9588e2591e2315e3744e1a30f9fa21", [
                ('    ":base32_rust",\n', ''),
            ]),
        "components/base32/base32.cc": (
            "35296e4139f333e7564b2f5a424283c059677a3d268b491a56a83c011dea0918", [
                ('#include "base/containers/span_rust.h"\n', ''),
                ('#include "base/strings/string_view_rust.h"\n', ''),
                ('#include "components/base32/base32.rs.h"\n', ''),
                ('rust::Base32EncodePolicy ToRustEncodePolicy(Base32EncodePolicy policy) {\n'
                 '  switch (policy) {\n'
                 '    case Base32EncodePolicy::INCLUDE_PADDING:\n'
                 '      return rust::Base32EncodePolicy::IncludePadding;\n'
                 '    case Base32EncodePolicy::OMIT_PADDING:\n'
                 '      return rust::Base32EncodePolicy::OmitPadding;\n'
                 '  }\n'
                 '  NOTREACHED();\n'
                 '}\n\n', ''),
                ('std::string Base32EncodeRust(base::span<const uint8_t> input,\n'
                 '                             Base32EncodePolicy policy) {\n'
                 '  std::string ret;\n'
                 '  rust::base32_encode(base::SpanToRustSlice(input), ToRustEncodePolicy(policy),\n'
                 '                      ret);\n'
                 '  return ret;\n'
                 '}\n\n'
                 'std::vector<uint8_t> Base32DecodeRust(std::string_view input) {\n'
                 '  std::vector<uint8_t> ret;\n'
                 '  if (!rust::base32_decode(base::StringViewToRustSlice(input), ret)) {\n'
                 '    return {};\n'
                 '  }\n'
                 '  return ret;\n'
                 '}\n\n', ''),
                ('  // This function may be called early enough that FeatureList is not set up\n'
                 '  // yet, in which case default to the legacy C++ implementation.\n'
                 '  if (base::FeatureList::GetInstance() != nullptr &&\n'
                 '      base::FeatureList::IsEnabled(features::kComponentsBase32InRust)) {\n'
                 '    return internal::Base32EncodeRust(input, policy);\n'
                 '  }\n'
                 '  return internal::Base32EncodeCpp(input, policy);',
                 '  return internal::Base32EncodeCpp(input, policy);'),
                ('  // This function may be called early enough that FeatureList is not set up\n'
                 '  // yet, in which case default to the legacy C++ implementation.\n'
                 '  if (base::FeatureList::GetInstance() != nullptr &&\n'
                 '      base::FeatureList::IsEnabled(features::kComponentsBase32InRust)) {\n'
                 '    return internal::Base32DecodeRust(input);\n'
                 '  }\n'
                 '  return internal::Base32DecodeCpp(input);',
                 '  return internal::Base32DecodeCpp(input);'),
            ]),
        "third_party/blink/renderer/platform/audio/fft_frame.h": (
            "49294818e9e0ff12b8884bab3faacbdb1ba7b6017a77a5d4db5ee86fc9f1d087", [
                ('#include "third_party/blink/renderer/platform/audio/audio_array.h"\n'
                 '#include "third_party/blink/renderer/platform/audio/rustfft_ffi.rs.h"\n',
                 '#include "third_party/blink/renderer/platform/audio/audio_array.h"\n'),
                ('#include "third_party/blink/renderer/platform/wtf/forward.h"\n'
                 '#include "third_party/rust/cxx/v1/cxx.h"\n',
                 '#include "third_party/blink/renderer/platform/wtf/forward.h"\n'),
                ('  AudioFloatArray real_data_;\n'
                 '  AudioFloatArray imag_data_;\n'
                 '\n'
                 '  rust::Box<blink::rust_fft::RustFft> rust_fft_;\n',
                 '  AudioFloatArray real_data_;\n'
                 '  AudioFloatArray imag_data_;\n'),
            ]),
        "third_party/blink/renderer/platform/audio/fft_frame.cc": (
            "49ca7087058cbd6d15b892e77b751ba87dcbb29dae984333a275252aad21accf", [
                ('#include <complex>\n#include <memory>\n',
                 '#include <algorithm>\n#include <cmath>\n#include <complex>\n'
                 '#include <memory>\n#include <utility>\n#include <vector>\n'),
                ('namespace {\n\nunsigned CheckedFftSize',
                 'namespace {\n\n'
                 'bool IsPowerOfTwo(unsigned n) { return n != 0 && (n & (n - 1)) == 0; }\n\n'
                 '// In-place radix-2 FFT for power-of-two sizes; naive DFT otherwise.\n'
                 'void Transform(std::vector<std::complex<double>>& a, bool inverse) {\n'
                 '  const unsigned n = static_cast<unsigned>(a.size());\n'
                 '  if (!IsPowerOfTwo(n)) {\n'
                 '    std::vector<std::complex<double>> out(n);\n'
                 '    const double sign = inverse ? 1.0 : -1.0;\n'
                 '    const double pi = std::acos(-1.0);\n'
                 '    for (unsigned k = 0; k < n; ++k) {\n'
                 '      std::complex<double> sum(0.0, 0.0);\n'
                 '      for (unsigned t = 0; t < n; ++t) {\n'
                 '        const double angle = sign * 2.0 * pi * k * t / n;\n'
                 '        sum += a[t] * std::complex<double>(std::cos(angle), std::sin(angle));\n'
                 '      }\n'
                 '      out[k] = inverse ? sum / static_cast<double>(n) : sum;\n'
                 '    }\n'
                 '    a = std::move(out);\n'
                 '    return;\n'
                 '  }\n'
                 '  for (unsigned i = 1, j = 0; i < n; ++i) {\n'
                 '    unsigned bit = n >> 1;\n'
                 '    for (; j & bit; bit >>= 1) j ^= bit;\n'
                 '    j ^= bit;\n'
                 '    if (i < j) std::swap(a[i], a[j]);\n'
                 '  }\n'
                 '  const double pi = std::acos(-1.0);\n'
                 '  for (unsigned len = 2; len <= n; len <<= 1) {\n'
                 '    const double angle = (inverse ? 2.0 : -2.0) * pi / len;\n'
                 '    const std::complex<double> wlen(std::cos(angle), std::sin(angle));\n'
                 '    for (unsigned i = 0; i < n; i += len) {\n'
                 '      std::complex<double> w(1.0, 0.0);\n'
                 '      for (unsigned j = 0; j < len / 2; ++j) {\n'
                 '        const std::complex<double> u = a[i + j];\n'
                 '        const std::complex<double> v = a[i + j + len / 2] * w;\n'
                 '        a[i + j] = u + v;\n'
                 '        a[i + j + len / 2] = u - v;\n'
                 '        w *= wlen;\n'
                 '      }\n'
                 '    }\n'
                 '  }\n'
                 '  if (inverse) {\n'
                 '    for (auto& x : a) x /= static_cast<double>(n);\n'
                 '  }\n'
                 '}\n\n'
                 'unsigned CheckedFftSize'),
                ('    : fft_size_(CheckedFftSize(fft_size)),\n'
                 '      rust_fft_(blink::rust_fft::rustfft_new(fft_size_)) {',
                 '    : fft_size_(CheckedFftSize(fft_size)) {'),
                ('void FFTFrame::DoFFT(base::span<const float> data) {\n'
                 '  CHECK_GE(data.size(), FftSize());\n'
                 '  rust_fft_->do_fft(rust::Slice<const float>(data.data(), fft_size_),\n'
                 '                    rust::Slice<float>(real_data_.Data(), real_data_.size()),\n'
                 '                    rust::Slice<float>(imag_data_.Data(), imag_data_.size()));\n'
                 '}',
                 'void FFTFrame::DoFFT(base::span<const float> data) {\n'
                 '  CHECK_GE(data.size(), FftSize());\n'
                 '  const unsigned n = fft_size_;\n'
                 '  std::vector<std::complex<double>> spectrum(n);\n'
                 '  for (unsigned i = 0; i < n; ++i) spectrum[i] = data[i];\n'
                 '  Transform(spectrum, false);\n'
                 '  const unsigned packed_size = (n + 1) / 2;\n'
                 '  if ((n & 1) == 0) {\n'
                 '    real_data_.Data()[0] = static_cast<float>(spectrum[0].real());\n'
                 '    imag_data_.Data()[0] = static_cast<float>(spectrum[n / 2].real());\n'
                 '    for (unsigned k = 1; k < packed_size; ++k) {\n'
                 '      real_data_.Data()[k] = static_cast<float>(spectrum[k].real());\n'
                 '      imag_data_.Data()[k] = static_cast<float>(spectrum[k].imag());\n'
                 '    }\n'
                 '  } else {\n'
                 '    for (unsigned k = 0; k < packed_size; ++k) {\n'
                 '      real_data_.Data()[k] = static_cast<float>(spectrum[k].real());\n'
                 '      imag_data_.Data()[k] = static_cast<float>(spectrum[k].imag());\n'
                 '    }\n'
                 '  }\n'
                 '}'),
                ('void FFTFrame::DoInverseFFT(base::span<float> data) {\n'
                 '  CHECK_GE(data.size(), FftSize());\n'
                 '  rust_fft_->do_inverse_fft(\n'
                 '      rust::Slice<const float>(real_data_.Data(), real_data_.size()),\n'
                 '      rust::Slice<const float>(imag_data_.Data(), imag_data_.size()),\n'
                 '      rust::Slice<float>(data.data(), fft_size_));\n'
                 '}',
                 'void FFTFrame::DoInverseFFT(base::span<float> data) {\n'
                 '  CHECK_GE(data.size(), FftSize());\n'
                 '  const unsigned n = fft_size_;\n'
                 '  std::vector<std::complex<double>> spectrum(n);\n'
                 '  if ((n & 1) == 0) {\n'
                 '    spectrum[0] = std::complex<double>(real_data_.Data()[0], 0.0);\n'
                 '    spectrum[n / 2] = std::complex<double>(imag_data_.Data()[0], 0.0);\n'
                 '    for (unsigned k = 1; k < n / 2; ++k) {\n'
                 '      spectrum[k] = std::complex<double>(real_data_.Data()[k],\n'
                 '                                         imag_data_.Data()[k]);\n'
                 '      spectrum[n - k] = std::conj(spectrum[k]);\n'
                 '    }\n'
                 '  } else {\n'
                 '    const unsigned packed_size = (n + 1) / 2;\n'
                 '    for (unsigned k = 0; k < packed_size; ++k) {\n'
                 '      spectrum[k] = std::complex<double>(real_data_.Data()[k],\n'
                 '                                         imag_data_.Data()[k]);\n'
                 '      if (k != 0) spectrum[n - k] = std::conj(spectrum[k]);\n'
                 '    }\n'
                 '  }\n'
                 '  Transform(spectrum, true);\n'
                 '  for (unsigned i = 0; i < n; ++i) data[i] = static_cast<float>(spectrum[i].real());\n'
                 '}'),
            ]),
        "third_party/blink/renderer/core/xml/build.gni": (            "5da61776d0fdf13ad8b98d2e75ad067d3ccb5e042d8d704f93c7e44ef0844a03", [
                ('  "parser/xml_document_parser.cc",\n'
                 '  "parser/xml_document_parser.h",\n'
                 '  "parser/xml_document_parser_rs.cc",\n'
                 '  "parser/xml_document_parser_rs.h",\n'
                 '  "parser/xml_document_parser_scope.cc",',
                 '  "parser/xml_document_parser.cc",\n'
                 '  "parser/xml_document_parser.h",\n'
                 '  "parser/xml_document_parser_scope.cc",'),
                ('  "parser/xml_parser_input.h",\n'
                 '  "parser/xml_ffi_callbacks.cc",\n'
                 '  "parser/xml_ffi_callbacks.h",\n'
                 '  "xml_serializer.cc",',
                 '  "parser/xml_parser_input.h",\n'
                 '  "xml_serializer.cc",'),
            ]),
        "third_party/blink/renderer/core/dom/document.cc": (
            "0604c18ae48fa9a22c3a685cdf38fcee9b4a0d1324262448fb7f61706556fbe8", [
                ('#include "third_party/blink/renderer/core/xml/parser/xml_document_parser.h"\n'
                 '#include "third_party/blink/renderer/core/xml/parser/xml_document_parser_rs.h"\n',
                 '#include "third_party/blink/renderer/core/xml/parser/xml_document_parser.h"\n'),
                ('  data_->using_rust_xml_parser_ = false;\n'
                 '\n'
                 '  // Use the Rust XML parser for situations non-XSLT situations: XMLHttpRequest,\n'
                 '  // JS DOMParser API, where no dom_window_ is available, or for SVG images.\n'
                 '  if (!GetFrame() || IsSVGDocument()) {\n'
                 '    // Measure this for now only in non-frame = non-XSLT situations, so that\n'
                 '    // when we compare the UMA metrics of Rust vs. non-Rust for\n'
                 '    // XMLRustForNonXsltEnabled(), we\'re looking at roughly the same type and\n'
                 '    // length of documents on average.\n'
                 '    data_->xml_parser_start_time_ = base::TimeTicks::Now();\n'
                 '\n'
                 '    if (RuntimeEnabledFeatures::XMLRustForNonXsltEnabled()) {\n'
                 '      data_->using_rust_xml_parser_ = true;\n'
                 '      return MakeGarbageCollected<XMLDocumentParserRs>(*this, View());\n'
                 '    }\n'
                 '  }\n'
                 '\n'
                 '  // FIXME: this should probably pass the frame instead\n'
                 '  if (RuntimeEnabledFeatures::XMLParsingRustEnabled()) {\n'
                 '    data_->using_rust_xml_parser_ = true;\n'
                 '    return MakeGarbageCollected<XMLDocumentParserRs>(*this, View());\n'
                 '  } else {\n'
                 '    return MakeGarbageCollected<XMLDocumentParser>(*this, View());\n'
                 '  }\n',
                 '  data_->using_rust_xml_parser_ = false;\n'
                 '  // CharmOS: C++ only. Use the existing libxml2 parser instead of the\n'
                 '  // Rust xml_ffi parser.\n'
                 '  return MakeGarbageCollected<XMLDocumentParser>(*this, View());\n'),
            ]),
        "content/shell/app/shell_main_delegate.cc": (
            "c22470ba2711a4c3c5a672da9d23519234aee785fb9d82a1ab03b97478a6b260", [
                ('  InitLogging(command_line);\n',
                 '  InitLogging(command_line);\n'
                 '\n'
                 '#if BUILDFLAG(IS_CHARMOS)\n'
                 '  // CharmOS has no fork/exec or zygote; render in the browser\n'
                 '  // process.\n'
                 '  if (command_line.GetSwitchValueASCII(switches::kProcessType).empty()) {\n'
                 '    command_line.AppendSwitch(switches::kSingleProcess);\n'
                 '  }\n'
                 '#endif\n'),
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 '#include "content/web_test/browser/web_test_browser_main_runner.h"  // nogncheck',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '#include "content/web_test/browser/web_test_browser_main_runner.h"  // nogncheck'),
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    // Instantiating',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    // Instantiating'),
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    // Web tests implement',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    // Web tests implement'),
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    browser_client_ =',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    browser_client_ ='),
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    renderer_client_ =',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  if (switches::IsRunWebTestsSwitchPresent()) {\n'
                 '    renderer_client_ ='),
                ('#include "content/shell/app/shell_main_delegate.h"', '#include "content/shell/app/shell_main_delegate.h"\n#include "build/charmos/startup_profile_charmos.h"'),
                ('std::optional<int> ShellMainDelegate::BasicStartupComplete() {', 'std::optional<int> ShellMainDelegate::BasicStartupComplete() {\n  CharmStartupStage("basic");'),
                ('void ShellMainDelegate::PreSandboxStartup() {', 'void ShellMainDelegate::PreSandboxStartup() {\n  CharmStartupStage("pre-sandbox");'),
                ('  InitializeResourceBundle();', '  CharmStartupStage("resource-start");\n  InitializeResourceBundle();\n  CharmStartupStage("resource-done");'),
                ('    MainFunctionParams main_function_params) {', '    MainFunctionParams main_function_params) {\n  CharmStartupStage("run-process");'),
            ]),
        "content/shell/app/shell_main_delegate.h": (
            "5948c20805ff067ebfcba616f2674de75234c07ef10286502de74e586b59fb61", [
                ('#if !BUILDFLAG(IS_ANDROID)\nnamespace ui {',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\nnamespace ui {'),
                ('#if !BUILDFLAG(IS_ANDROID)\nclass WebTestBrowserMainRunner;',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 'class WebTestBrowserMainRunner;'),
                ('#if !BUILDFLAG(IS_ANDROID)\n  // Only present when running web tests',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  // Only present when running web tests'),
            ]),
        "content/shell/browser/shell.h": (
            "e016c5b6012018cc2b3075034fb03ded712d9c81c8c194ed00534959e80c9643", [
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 '  void RegisterProtocolHandler(RenderFrameHost* requesting_frame,',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 '  void RegisterProtocolHandler(RenderFrameHost* requesting_frame,'),
                ('  WebContents* web_contents() const { return web_contents_.get(); }',
                 '  WebContents* web_contents() const { return web_contents_.get(); }\n\n'
                 '  void NewTab(BrowserContext* browser_context, const GURL& url);\n'
                 '  void SelectTab(size_t index);\n'
                 '  void CloseActiveTab();\n'
                 '  size_t tab_count() const { return inactive_tabs_.size() + 1; }\n'
                 '  std::u16string TabTitle(size_t index) const;'),
                ('  std::unique_ptr<WebContents> web_contents_;',
                 '  std::unique_ptr<WebContents> web_contents_;\n'
                 '  std::vector<std::unique_ptr<WebContents>> inactive_tabs_;\n\n'
                 '  void ShowActiveTab();'),
            ]),
        "content/shell/browser/shell.cc": (
            "5b10c613e0a1bd3d49bafc5c9a6d11f6bbc390cf4034f55fe67e626670c128ff", [
                ('#if !BUILDFLAG(IS_ANDROID)\n'
                 'void Shell::RegisterProtocolHandler(RenderFrameHost* requesting_frame,',
                 '#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHARMOS)\n'
                 'void Shell::RegisterProtocolHandler(RenderFrameHost* requesting_frame,'),
                ('void Shell::RenderFrameCreated(RenderFrameHost* frame_host) {',
                 'void Shell::ShowActiveTab() {\n'
                 '  web_contents_->SetDelegate(this);\n'
                 '  Observe(web_contents_.get());\n'
                 '  g_platform->DidCreateOrAttachWebContents(this, web_contents_.get());\n'
                 '  g_platform->SetContents(this);\n'
                 '  g_platform->SetAddressBarURL(this, web_contents_->GetLastCommittedURL());\n'
                 '  g_platform->SetTitle(this, web_contents_->GetTitle());\n'
                 '  UpdateNavigationControls(false);\n'
                 '  web_contents_->Focus();\n'
                 '}\n\n'
                 'void Shell::NewTab(BrowserContext* browser_context, const GURL& url) {\n'
                 '  inactive_tabs_.push_back(std::move(web_contents_));\n'
                 '  WebContents::CreateParams params(browser_context);\n'
                 '  web_contents_ = WebContents::Create(params);\n'
                 '  ShowActiveTab();\n'
                 '  LOG(WARNING) << "charmos-ui: new-tab " << inactive_tabs_.size() + 1\n'
                 '               << " incognito=" << browser_context->IsOffTheRecord();\n'
                 '  if (!url.is_empty()) LoadURL(url);\n'
                 '}\n\n'
                 'void Shell::SelectTab(size_t index) {\n'
                 '  if (index >= inactive_tabs_.size()) return;\n'
                 '  web_contents_.swap(inactive_tabs_[index]);\n'
                 '  ShowActiveTab();\n'
                 '  LOG(WARNING) << "charmos-ui: select-tab " << index;\n'
                 '}\n\n'
                 'void Shell::CloseActiveTab() {\n'
                 '  if (inactive_tabs_.empty()) {\n'
                 '    Close();\n'
                 '    return;\n'
                 '  }\n'
                 '  Observe(nullptr);\n'
                 '  web_contents_ = std::move(inactive_tabs_.back());\n'
                 '  inactive_tabs_.pop_back();\n'
                 '  ShowActiveTab();\n'
                 '  LOG(WARNING) << "charmos-ui: close-tab " << inactive_tabs_.size() + 1;\n'
                 '}\n\n'
                 'std::u16string Shell::TabTitle(size_t index) const {\n'
                 '  if (index < inactive_tabs_.size()) return inactive_tabs_[index]->GetTitle();\n'
                 '  return web_contents_->GetTitle();\n'
                 '}\n\n'
                 'void Shell::RenderFrameCreated(RenderFrameHost* frame_host) {'),
            ]),
        "content/shell/utility/shell_content_utility_client.h": (
            "e03b6eeafbfd25d7145e9a9e6d505f222979d38be5073782ef3db257d1e01254", [
                ('#include "content/public/test/audio_service_test_helper.h"\n'
                 '#include "content/public/test/network_service_test_helper.h"\n'
                 '#include "content/public/utility/content_utility_client.h"',
                 '#include "build/build_config.h"\n'
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '#include "content/public/test/audio_service_test_helper.h"\n'
                 '#include "content/public/test/network_service_test_helper.h"\n'
                 '#endif\n'
                 '#include "content/public/utility/content_utility_client.h"'),
                ('  std::unique_ptr<NetworkServiceTestHelper> network_service_test_helper_;\n'
                 '  std::unique_ptr<AudioServiceTestHelper> audio_service_test_helper_;\n',
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '  std::unique_ptr<NetworkServiceTestHelper> network_service_test_helper_;\n'
                 '  std::unique_ptr<AudioServiceTestHelper> audio_service_test_helper_;\n'
                 '#endif\n'),
            ]),
        "content/shell/utility/shell_content_utility_client.cc": (
            "717a140d90467bcea90f1350d3f8e95a1f203b66b186901a58cfdac56a6d2e32", [
                ('    network_service_test_helper_ = NetworkServiceTestHelper::Create();\n'
                 '    audio_service_test_helper_ = std::make_unique<AudioServiceTestHelper>();\n'
                 '    storage::InjectTestApiImplementation();\n',
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '    network_service_test_helper_ = NetworkServiceTestHelper::Create();\n'
                 '    audio_service_test_helper_ = std::make_unique<AudioServiceTestHelper>();\n'
                 '    storage::InjectTestApiImplementation();\n'
                 '#endif\n'),
            ]),
        "content/shell/renderer/shell_render_frame_observer.cc": (
            "2de42c455df1255576027f3dc1846fbdebfbc9ad0baaa12d0ee96fbae7120688", [
                ('  if (cmd.HasSwitch(switches::kExposeInternalsForTesting)) {\n'
                 '    blink::WebTestingSupport::InjectInternalsObject(\n'
                 '        render_frame()->GetWebFrame());\n'
                 '  }',
                 '  if (cmd.HasSwitch(switches::kExposeInternalsForTesting)) {\n'
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '    blink::WebTestingSupport::InjectInternalsObject(\n'
                 '        render_frame()->GetWebFrame());\n'
                 '#endif\n'
                 '  }'),
            ]),
        "content/shell/renderer/shell_content_renderer_client.cc": (
            "5ec008d178c3faa59db76c2d3e7fed59f940696fa1c77f01580122707413dcbc", [
                ('  if (base::CommandLine::ForCurrentProcess()->HasSwitch(\n'
                 '          switches::kExposeInternalsForTesting)) {\n'
                 '    blink::WebTestingSupport::InjectInternalsObject(context);\n'
                 '  }',
                 '  if (base::CommandLine::ForCurrentProcess()->HasSwitch(\n'
                 '          switches::kExposeInternalsForTesting)) {\n'
                 '#if !BUILDFLAG(IS_CHARMOS)\n'
                 '    blink::WebTestingSupport::InjectInternalsObject(context);\n'
                 '#endif\n'
                 '  }'),
            ]),
        "third_party/blink/renderer/platform/fonts/skia/sktypeface_factory.cc": (
            "ec0fa7b3780fc4b8e6c607089475e495072292d7762dc5e91115932adcbcd269", [
                ('  sk_sp<SkFontConfigInterface> fci(SkFontConfigInterface::RefGlobal());\n'
                 '  SkFontConfigInterface::FontIdentity font_identity;\n'
                 '  font_identity.fID = config_id;\n'
                 '  font_identity.fTTCIndex = ttc_index;\n'
                 '  return fci->makeTypeface(font_identity, SkFontMgr_New_Fontations_Empty());',
                 '#if BUILDFLAG(IS_CHARMOS)\n'
                 '  // CharmOS has no fontconfig identity database; the directory font\n'
                 '  // manager resolves faces by family or filename instead.\n'
                 '  (void)config_id;\n'
                 '  (void)ttc_index;\n'
                 '  return nullptr;\n'
                 '#else\n'
                 '  sk_sp<SkFontConfigInterface> fci(SkFontConfigInterface::RefGlobal());\n'
                 '  SkFontConfigInterface::FontIdentity font_identity;\n'
                 '  font_identity.fID = config_id;\n'
                 '  font_identity.fTTCIndex = ttc_index;\n'
                 '  return fci->makeTypeface(font_identity, SkFontMgr_New_Fontations_Empty());\n'
                 '#endif'),
                ('  return SkFontMgr_New_Fontations_Empty()->makeFromFile(filename.c_str(),\n'
                 '                                                        ttc_index);',
                 '#if BUILDFLAG(IS_CHARMOS)\n'
                 '  return skia::DefaultFontMgr()->makeFromFile(filename.c_str(), ttc_index);\n'
                 '#else\n'
                 '  return SkFontMgr_New_Fontations_Empty()->makeFromFile(filename.c_str(),\n'
                 '                                                        ttc_index);\n'
                 '#endif'),
            ]),
        "third_party/blink/renderer/platform/image-decoders/skia/skia_image_decoder_base.cc": (
            "b01c8d259eaf2f6ef89cd5dc16049b2675a5c85fac8ce38d33c5ba8436494545", [
                ('#include <limits>', '#include <limits>\n#include "build/build_config.h"\n#if BUILDFLAG(IS_CHARMOS)\n#include "build/charmos/static_codec_frame_charmos.h"\n#endif'),
                ('namespace {', 'namespace {\nbool GetFrameInfoForBlink(SkCodec* codec, bool all_received, int index,\n                          SkCodec::FrameInfo* info) {\n#if BUILDFLAG(IS_CHARMOS)\n  return charmos::CodecFrameInfo(codec, all_received, index, info);\n#else\n  return codec->getFrameInfo(index, info);\n#endif\n}\n'),
                ('!codec_->getFrameInfo(index, &frame_info)', '!GetFrameInfoForBlink(codec_.get(), IsAllDataReceived(), index, &frame_info)'),
                ('bool frame_info_received = codec_->getFrameInfo(index, &frame_info);', 'bool frame_info_received = GetFrameInfoForBlink(codec_.get(), IsAllDataReceived(), index, &frame_info);'),
                ('\n          codec_->getFrameInfo(current_frame_index, &frame_info);', '\n          GetFrameInfoForBlink(codec_.get(), IsAllDataReceived(), current_frame_index, &frame_info);'),
                ('\n            codec_->getFrameInfo(current_frame_index, &frame_info);', '\n            GetFrameInfoForBlink(codec_.get(), IsAllDataReceived(), current_frame_index, &frame_info);'),
            ]),

    }
