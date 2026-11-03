# Chromium native software graphics profile

The native profile uses the pinned Chromium Skia implementation with its
existing C++ libpng PNG reader/writer, BMP/ICO/JPEG codecs, WebP, Wuffs and
FreeType backends. Rust bridges and Fontations are not in its dependency
closure. EXIF and ICC handling remain in Skia's existing C++ implementation.
Code and licenses remain upstream; only backend selection and OS integration
are downstream. The graph audit must check the graphics executable itself.
Skia's `SK_BUILD_FOR_UNIX` selects its POSIX-style file/locale path backed by
native libc. Chromium identity remains IS_CHARMOS, with IS_LINUX and IS_APPLE
false; no host SDK headers or host libraries are used. Without an explicit
Skia platform, its unknown-target fallback would incorrectly select macOS.

Rendering uses CPU raster surfaces. This profile does not advertise working
GPU acceleration: no GL, Vulkan or Dawn native context exists yet. Raster
pixels are submitted to the compositor with `Window::present_pixels`, which
copies from engine-owned storage to an available compositor buffer. A frame
acknowledgment returns that buffer to the client. Ownership does not transfer
through a host pointer, and the source raster remains owned by its engine.
This same submission API can be used by another browser or rendering engine.

Skia's native default font manager enumerates real files in `/fonts` using
its FreeType directory implementation. Packages must install their font files
there and retain their font licenses. It does not claim that the empty font
manager provides system fonts. The graphics test image installs Space Grotesk
and JetBrains Mono from the project's existing OFL assets.

`graphics_runtime` is a graphics integration probe, not a browser. It verifies
raster drawing, actual font glyphs, PNG encoding/decoding, and submission to a
real WM window. A test invocation waits for frame completion; normal invocation
keeps the window open and handles native key and close events. Ozone, web engine
integration and browser UI remain separate required work.

The Chromium adapter `WindowEventSourceCharmOS` watches a borrowed SDK Window
on a Chromium UI task executor. It owns a readiness-only FD duplicate, consumes
events through Window::wait (so buffer ownership is updated before delivery),
and unregisters its controller before closing the FD. Start/Stop/destruction
and callbacks run on that UI sequence. The Window must outlive the source's
registration. Stop is idempotent; callbacks may Stop, restart or destroy their
source. Error delivery unregisters first. Dispatch is bounded to 64 messages
per notification to allow other Chromium tasks to run. This adapter is shared
glue for the native Ozone backend, without changing the engine-independent
window ABI.
