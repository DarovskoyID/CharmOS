# CharmOS application API

Public, engine-independent C++ headers live in `include/charm`. They do not
require Chromium or a C++ standard library. Existing native applications and
future browser engines use the same capability ABI. The first convenience
layer is SDK 1.0; its ownership/memory helpers require syscall ABI 1.16.

| Need | Public API |
|---|---|
| Own, duplicate, transfer a capability | `charm/handle.hpp`, `UniqueHandle` |
| Query, terminate, wait for a process capability | `charm/process.hpp`, borrowed `ProcessView` |
| Create/import/map shared memory | `charm/memory.hpp`, `SharedMemory`, `Mapping` |
| Window, double buffers, input, frame acknowledgements | `charm/window.hpp`, `charm::gui::Window`, CMake `wm_client` |
| FD readiness, files, sockets, threads | native libc `poll`, `fcntl`, `sys/socket.h`, `pthread.h` |
| Channels, cancellable readiness ports, explicit capabilities | `charm/sys.hpp` |
| Launch with explicit filesystem/network/window authority | `charm/libc.h`, `charm_spawn` |
| Take a received application capability once by role | `charm/libc.h`, `charm_take_start_handle` (SDK 1.1) |
| Bounded byte streams, writable waits and shutdown | `charm/stream.hpp`, borrowed `ChannelStream` (ABI 1.18) |
| Native channel readiness in an FD-based event loop | `charm/libc.h`, `charm_channel_watch_fd`; close the returned FD |

The SDK provides OS integration, not an HTML/JavaScript engine. GPU integration,
POSIX fork/exec, dynamic libraries, executable VMOs and arbitrary protection
changes are currently unavailable. Check returned Status/errno; do not assume
a service or permission exists because its header compiles. ABI contracts and
current limitations are in `docs/abi/application-sdk.md` and the linked specs.

## Shared pixels or IPC data

```cpp
#include <charm/memory.hpp>

charm::SharedMemory pixels;
auto status=charm::SharedMemory::create(640*480*4,&pixels);
if(status!=charm::Status::Ok) return status;
charm::Mapping drawing;
status=pixels.map(charm::MemoryAccess::ReadWrite,&drawing);
if(status!=charm::Status::Ok) return status;
auto* data=static_cast<unsigned char*>(drawing.data());
data[0]=255;

charm::UniqueHandle for_reader;
status=pixels.share(charm::rights::kRead|charm::rights::kMap|
                    charm::rights::kTransfer,&for_reader);
if(status!=charm::Status::Ok) return status;
// for_reader has no Write or Duplicate right. The reader can map, not modify.
// drawing and pixels each clean up their own distinct resource automatically.
```

`create` rounds positive sizes to pages. `size()` reports the actual allocation
size; keep a separate logical data length and validate it against that size.
Imported objects are checked with VmoSize before the SDK accepts ownership.
An unsuccessful create/map/adopt leaves existing output objects unchanged.

## Transfer without double-close

```cpp
charm::Handle give=for_reader.get(); // borrow while attempting the send
status=charm::channel_write(channel,&message,sizeof message,&give,1);
if(status==charm::Status::Ok) (void)for_reader.release();
// On failure for_reader still owns the capability and its destructor closes it.
```

After a successful channel read, adopt each returned handle exactly once:

```cpp
auto incoming=charm::UniqueHandle::adopt(received_handle);
charm::SharedMemory imported;
status=charm::SharedMemory::adopt(static_cast<charm::UniqueHandle&&>(incoming),&imported);
if(status!=charm::Status::Ok) return status;
charm::Mapping reading;
status=imported.map(charm::MemoryAccess::ReadOnly,&reading);
```

The cast is the freestanding equivalent of `std::move`. An engine using libc++
can use `std::move(incoming)` normally. `get()`/`handle()`/`data()` borrow;
`release()` transfers cleanup responsibility. Never make two owners from one
borrowed handle. Closing a VMO handle leaves its mappings alive; revoking an
ancestor capability can invalidate them under the existing kernel contract.

## Integrate the window into an engine loop

Create a `charm::gui::Window`, then observe its borrowed `event_handle()` using
ObjectWatch readable notifications. Consume every event through Window::wait
and drain next_input before waiting again. FRAME_DONE returns ownership of a
specific buffer: pixels(index) is null while that buffer belongs to the
compositor. Present only an owned buffer and the changed rectangle. Rendering
must respect format(), width() and height(). Window destruction closes the
connection and releases mappings. See `user/libwm/README.md` and `wdemo`.

Adapters for different engines should stay outside this SDK. Chromium-specific
code currently lives in `tools/chromium/platform`; its Perfetto allocation and
shared-memory adaptation consume these same public ownership primitives.

## Process lifetime and deadlines

`charm_spawn` returns an owned process capability. Its `give` bits explicitly
choose which existing services to pass; zero grants none. Additional handles
are moved according to the existing startup protocol. Argument/environment
and handle limits fail before launch rather than dropping entries.

```cpp
#include <charm/process.hpp>
auto owner=charm::UniqueHandle::adopt(process_handle);
charm::ProcessView child(owner.get());
int64_t code;
auto status=child.wait(charm::now_ns()+5'000'000'000,&code);
if(status==charm::Status::TimedOut) {
  // Decide whether to keep waiting, request termination, or retain the handle.
}
```

Wait cancellation releases native port subscriptions on every exit path.
Interrupted retries must retain the original absolute deadline. A process PID
does not permit opening/killing it: the kernel capability does. Closing owner
does not terminate the child; retaining owner preserves exit metadata even
after a successful wait. ProcessView borrows and never closes the capability.
It requires that the owner remains alive for the call. `duplicate_same` queries
the actual rights and duplicates them; it never broadens the capability.

Application roles start at 0x10000. Assign a role in `extra_roles` when spawning;
the receiver calls `charm_take_start_handle(role)` and immediately adopts the
result with `UniqueHandle::adopt`. Concurrent callers cannot take the same
handle twice. The role only selects an already received capability; numbers
from another process do not name or grant access to its objects.
