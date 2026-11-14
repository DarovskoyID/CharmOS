# WM v3: managed native windows

This is an additive application channel protocol. Kernel ABI remains 1.18.
WM v2 and its message layouts remain accepted, and SDK Window::create keeps
using v2. Window::create_managed explicitly selects v3; it does not silently
claim v3 features when connecting to an older compositor.

## Connection and creation

HELO remains two uint32 fields (type=1, version=3) and transfers the private
server channel endpoint as in v2. WELCOME v3 is 44 bytes: the existing Welcome
prefix (20 bytes), uint32 title_height at20, int32 work_x/work_y at24/28,
uint32 work_width/work_height at32/36, uint32 features at40. Work bounds and
title inset describe the actual compositor layout. Features: title=1,
bounds=2, visibility=4, activation=8. Clients check the relevant bit before
issuing a request. Unknown capability bits do not imply supported methods.

CREATE v3 is the existing Create prefix plus uint32 flags at76 (80 bytes).
Flag1 starts minimized/invisible, flag2 suppresses initial activation. Other
bits are rejected. Both read-only pixel VMO capabilities and the Created
reply/input ring are as in v2. Generation starts at1. Native SDK clamps initial
content size to the advertised work area excluding the title inset. Getters
report the actual size and content origin after creation.

## Events and frame ownership

All v3 events are 32 bytes: the existing WindowEvent prefix (12 bytes), uint32
generation at12, int32 content_x/content_y at16/20, uint32 content_width/
content_height at24/28. Original event types retain their meaning. New types:
CONFIGURED=107, BOUNDS_CHANGED=108, VISIBILITY_CHANGED=109. Visible value is
0/1; CONFIGURED value is 0 for acceptance and 1 for rejection.

PRESENT v3 is the existing Present prefix plus uint32 generation at28 (32
bytes). It must match the current generation. Buffer0 is the first usable
buffer after creation or reconfiguration; buffer1 becomes usable on its first
FRAME_DONE1. Later ownership follows the existing v2 release semantics.
Clients do not submit frames while reconfiguration is pending.

## Reconfiguration

CONFIGURE type5 is 32 bytes: uint32 type, window, generation, width, height
at0..16; int32 desired content_x/content_y at20/24; reserved uint32 zero at28.
It transfers exactly two new read-only mapped pixel VMOs. The generation must
equal current+1; wrapping is not supported. Content dimensions must fit the
advertised work area including title inset; origin is clamped to that area.
Before accepting, the server validates both immutable VMO sizes and maps both
read-only. Failure releases every transferred capability and partial map,
leaves the old window intact, and returns CONFIGURED(rejected) with the
requested generation and the old actual geometry.

On acceptance the compositor retires old pending presentation, replaces both
maps, resets shown/pending indices, updates generation and actual geometry,
and sends CONFIGURED(accepted). Any retirement FRAME_DONE is sent before the
CONFIGURED reply. The compositor never accesses old buffers after that reply.
The client keeps both old and proposed maps until CONFIGURED. Acceptance
replaces the old maps; rejection releases only proposed maps. Channel ordering
keeps old generation events before the commit reply. Later events use the new
generation. A client may have at most one reconfiguration pending. Closing
the window cancels local ownership; no nonexistent successful resize is
reported on timeout or peer closure.

SDK request_bounds returns after submitting the request. Window::wait applies
the reply before delivering CONFIGURED. Configuration getters expose pending
state; pixel access/presentation is blocked during that state. Apps resume
painting with the actual dimensions after CONFIGURED, and inspect its result.

## Other controls

- SET_TITLE type6: uint32 type/window, char title[64] (72 bytes), NUL terminated.
  The compositor updates the real title and damages its decoration.
- SET_POSITION type7: uint32 type/window, int32 content_x/content_y (16 bytes).
  It changes no pixel maps; actual clamped geometry arrives as BOUNDS_CHANGED.
- SET_VISIBILITY type8: uint32 type/window/visible/activate (16 bytes), bool
  fields limited to0/1. Invisible corresponds to the compositor's actual
  minimized state. Showing optionally raises/focuses the window; hiding moves
  focus to another visible window. VISIBILITY_CHANGED reports actual state.
- ACTIVATE type9: uint32 type/window/active (12 bytes). Activation restores,
  raises and focuses that window; deactivation clears focus if it owns it.
  Existing FOCUS events report actual activation. No input is injected.

Controls and IDs are scoped to the connection that owns the window. Invalid
wire shapes, reserved flags, IDs or generations close that connection. Normal
configuration resource/range failures use the rejection reply. This extension
does not yet advertise fullscreen or custom cursor bitmaps. Popup windows and
client-drawn top-level frames are described separately in `wm.md`.

## Pointer capture capability

Feature bit16 advertises pointer capture. SET_CAPTURE type10 is 12 bytes:
uint32 type/window/captured, with the last field limited to0/1. CAPTURE_CHANGED
event110 reports actual capture ownership (value0/1), using the regular v3
event geometry and generation. Capture is connection scoped. A focused, visible
window can acquire it while no shell modal UI or title-bar drag is active. A
visible popup created with `kCreatePopup` can also acquire it without activation;
its parent retains focus and keyboard routing.
An ineligible request is acknowledged with value0; it never steals capture
from another window. Releasing a window that does not own capture likewise
reports0 and does not affect the actual owner.

Captured motion, button and wheel events go to that window even outside its
content rectangle, with signed window-local coordinates that may be negative
or exceed its size. The desktop pointer remains visible and clamped to the
screen. Captured clicks do not activate other windows or desktop controls.
Keyboard routing and shell keyboard shortcuts remain independent of capture.
Losing focus, hiding, destruction/disconnection, or opening a shell modal
releases capture; a surviving connection receives CAPTURE_CHANGED(0) before
the corresponding focus-loss notification. Acquisition/release never injects
synthetic button events. Existing v2 connections are unchanged.

The SDK set_capture method submits a command; captured() changes only when
Window::wait consumes CAPTURE_CHANGED. Unsupported v2/feature masks return
NotSupported. This does not advertise cursor warping, confinement or bitmaps.

## Output backpressure

The compositor keeps a bounded FIFO of64 outbound messages per connection,
including any capability awaiting transfer. Queued capability ownership stays
with the compositor until channel_write succeeds, or is closed on disconnect.
Read/write object watches flush the FIFO when space becomes available. Events
are never silently discarded on WouldBlock. Overflow or a failed connection
marks that client for cleanup outside event-dispatch callbacks, avoiding
reentrant destruction of window state. Other clients continue to run.
