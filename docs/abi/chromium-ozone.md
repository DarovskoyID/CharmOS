# Native Chromium Ozone platform

Status: integration under review. The independent native canvas and WM SDK
are guest-verified; the registered Ozone platform still requires a successful
full build and the ozone_runtime guest regression before completion is claimed.

This document defines the CharmOS Ozone integration. It adds no syscall or WM
wire messages. WM v3 remains the window protocol; unsupported window features
must be added to that protocol before Chromium exposes them as working.

## Registration and process model

The C++-only native GN profile registers platform name `charmos` through the
standard Ozone external-platform lists. `CreateOzonePlatformCharmos` and
`CreateClientNativePixmapFactoryCharmos` are the standard generated-constructor
entry points. The initial profile is software-only: no GL, Vulkan, native
pixmap, overlay, hardware cursor, dynamic loader or separate GPU process is
advertised. Stub input/overlay/GPU-support interfaces denote unavailable
services, not successful hardware emulation.

Ozone UI initialization discovers the real display and work area through a
hidden, inactive WM v3 connection. Failure to connect or discover a nonempty
display fails initialization. A single real display is exposed initially.
The platform owns the registry, surface factory, screen, event source and
standard minimal input method.

## Window registry and lifetime

Every native window receives a nonzero process-local `uint32_t`
`gfx::AcceleratedWidget` from `base::IDMap`. Native pointers never cross a
process boundary and are never used as widget IDs. The registry maps IDs to
`PlatformWindowCharmOS` objects on the UI sequence. A separately locked table
holds strong presenter references for render-thread factory lookup. Raw window
fields are never read from that thread. `SurfaceFactoryCharmOS`
resolves that ID and creates `SurfaceOzoneCanvasCharmOS` for the same
`NativeWindowPresenterCharmOS` that owns the window and its FD watcher.

Construction creates the managed native window before reporting
`OnAcceleratedWidgetAvailable`. Destruction unregisters the ID, stops event
delivery and closes native resources. A connection failure reports widget
destruction/closure and completes a pending swap with an empty size. Callbacks
may destroy their window; implementations use weak pointers or retain the
presenter across callbacks as appropriate.

## Geometry, state and events

Bounds are physical pixels with scale 1.0 in the initial profile. They describe
the client content area, as do WM v3 events. Requests are asynchronous: getters
change only after `CONFIGURED` or `BOUNDS_CHANGED`; rejected configuration does
not update Chromium geometry. Focus, visibility and close notifications map to
the corresponding delegate callbacks. Minimize/restore use real WM visibility.
Native decorations remain enabled.

Pointer capture uses the negotiated WM feature16 and actual CAPTURE_CHANGED
notifications. Fullscreen, borderless/popup windows, cursor bitmaps, cursor
warping and confinement require real WM capabilities. Until then the platform
must not report those states as acquired or completed. Later protocol additions
must be additive and feature negotiated.

## Input

WM HID usage values map directly to `ui::DomCode` USB values. KeyboardCode and
nonprintable DomKey use Chromium's standard US-layout conversion. The translator
tracks Shift, Control, Alt, GUI, CapsLock and pressed mouse buttons. Printable
key presses additionally emit `ET_CHAR`, including the existing English/Russian
BMP mapping; Control/Alt shortcuts do not fabricate text.

Pointer and wheel events carry real window-local coordinates, timestamps,
pressed-button flags and changed-button flags. Motion uses dragged type when a
button is held. Unknown HID usages and unsupported buttons are ignored rather
than mapped to an unrelated key.

## Presentation

The factory returns the existing real upstream `SurfaceOzoneCanvas` subclass.
It retains one pending swap, uses immutable raster snapshots and returns the
actual size only after native `FRAME_DONE`. Resize invalidates an in-flight
presentation; a stale frame cannot be reported as displayed. There is no
estimated VSync timer in place of scanout.

## Verification

The native network configuration selects CHROME_ROOT_STORE_ONLY and Chromium's
actual Chrome-only trust store and BoringSSL verifier, with no host NSS database
or invented Fuchsia verifier. The offline guest probe checks a valid test chain
using an instance-local test anchor, then rejects an unknown issuer, wrong host,
expired certificate and damaged signature. Test anchors never alter production
trust. The downstream variations platform uses its own PLATFORM_CHARMOS=1000,
not a Linux identity; upstream reserved enum values are preserved.

Unsupported legacy Pickle VMO attachments must fail deserialization; they cannot
be reported as successful empty regions. Native pixmaps remain unavailable in
the software profile and are rejected rather than silently cloned as empty.

The native PathService provider returns existing, explicitly configured cache
and desktop directories. FILE_EXE and FILE_MODULE fail unless a launcher
overrides them: the startup ABI has no authoritative executable path. argv[0]
and host /proc paths are not substitutes. Packaged resources use an explicit
DIR_ASSETS override.

Abseil's low-level allocator uses the genuine native anonymous read/write mmap
subset. Its ordinary allocator, thread identities and waiters are enabled;
async-signal-safe arenas remain unavailable because POSIX signal masking is not
provided. No absent allocator or semaphore is replaced by a successful stub.

MemoryMappedFile uses genuine native private snapshots for READ_ONLY and
READ_WRITE_COPY. Region alignment padding stops at actual EOF, and a separate
base/length pair owns the complete mapping even when the exposed view starts
at an unaligned offset. READ_WRITE and READ_WRITE_EXTEND fail before changing
the backing file; shared/writeback mappings remain unsupported. ICU data is
read from its actual packaged file through this backend.

A guest `ozone_runtime` executable initializes the registered platform, creates
a window through `OzonePlatform::CreatePlatformWindow`, obtains canvas by
`AcceleratedWidget`, presents real pixels, and exercises delegate geometry,
keyboard/text, mouse/wheel, activation, resize, visibility and close paths.
QEMU checks both serial events and screen pixels. This probe is not itself a
browser and is not used to claim that content, Blink or Chrome UI are complete.
