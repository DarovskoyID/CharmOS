# Native Chromium software canvas

The initial software Ozone canvas uses the upstream SurfaceOzoneCanvas
interface and the WM v3 SDK. This is an in-process UI/render-thread bridge,
not a claim that a separate GPU process or the complete Ozone platform works.
No widget pointer or process-local address is sent across a channel.

NativeWindowPresenterCharmOS owns the WM connection on its UI sequence.
Window events are consumed only by WindowEventSourceCharmOS, so SDK ownership
and actual geometry are applied before callbacks. Its event observer may
drain input but must not independently read the event channel. Closing first
stops the watch, then closes the window. Final destruction is on the UI
sequence even when the last reference belongs to the render thread.
The borrowed window accessor is for geometry, input draining and controls;
pixel submission belongs exclusively to the presenter. Mixing direct present
calls with presenter swaps would make frame ownership ambiguous.

SurfaceOzoneCanvasCharmOS belongs to its creating render sequence. Resize
allocates a persistent opaque BGRA raster surface; GetCanvas returns its
canvas. PresentCanvas snapshots the complete frame, preserving pixels outside
damage across alternating native buffers. OnSwapBuffers posts that immutable
image and damage to the UI owner. The UI copies through present_pixels and
retains the swap callback until a real compositor FrameDone. Completion is
posted back to the render sequence. There is at most one pending swap per
surface. No estimated timer is used as a presentation acknowledgment.

The viewport must match the actual native content size. A pending native
reconfiguration defers submission until its acknowledgment; if the actual
size differs, completion carries an empty gfx::Size. Native closure, invalid
damage, allocation failure or a second concurrent swap also complete with an
empty size. This API has no swap-result parameter; callers must treat empty
size as failure. No successful swap is fabricated on an error. Shutdown
cancels retained callbacks and drops snapshots. A render-thread callback is
never called synchronously from the UI event-dispatch stack.

Native request_bounds cancels any outstanding frame before retiring old
buffers. The configured event exposes accepted geometry before repainting.
The bridge does not supply a VSyncProvider: WM frame acknowledgment does not
provide measured scanout timing. Scale must be finite and positive; allocation
uses physical viewport pixels. GL, Vulkan and hardware acceleration are not
advertised by this component.
