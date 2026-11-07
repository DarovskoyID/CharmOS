# Native Chromium Mojo transport

Status: ChannelCharmOS, capability/bootstrap adapters and new guest probes are
prepared in the downstream overlay, but not compiled or guest-verified yet.
NativeChannel/ChannelStream SDK tests do not verify Mojo. Completion requires
the public Mojo invitation/message-pipe probe in two actual processes.

Mojo uses CharmOS kernel channels and capabilities. It does not emulate Unix
stream sockets, SCM_RIGHTS, PID authority or FD-valued shared memory.

## Handles

A native Mojo `PlatformHandle` has a distinct CharmOS capability type and owns a
`charm::UniqueHandle`. Cloning duplicates actual kernel rights; transfer moves
ownership. File and VMO capabilities retain their native object identity and
rights. A small poll-only libc FD may be used by the local I/O watcher, but it
is never serialized as a Mojo handle or treated as the transmitted object.

The public Mojo C representation uses a dedicated CharmOS type value rather
than `MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR`. This is part of the native
port contract and does not change other platforms' values.
The native type value is7; the existing0..6 values are unchanged. Its value
field holds a local uint32 capability extended to uint64; out-of-range values
are rejected before adopting ownership. Duplication preserves actual rights
and may fail if Duplicate is absent. Process-handle cloning validates a real
process capability and duplicates it; a PID never grants process authority.

## Channel

`ChannelCharmOS` implements `mojo::core::Channel` over a message-preserving
kernel channel endpoint. Each write carries the complete Mojo message and its
capability batch when it fits the kernel limits: 64 KiB and16 capabilities per
kernel message. Larger Mojo messages require an explicitly versioned framing
and bounded reassembly protocol, including capability batches; queueing alone
does not remove these limits. Until that framing is implemented and tested,
oversized messages must fail explicitly rather than truncate data or handles.
Bounded output queues handle backpressure; partial stream semantics are not
invented. Readiness uses `charm_channel_watch_fd` on
the designated Mojo I/O sequence. Peer closure and malformed messages map to the
channel error callbacks.

The initial transport supports the current ipcz driver and normal Channel
messages. Legacy Apple/Linux/Android upgrade and multi-notifier extensions are
not advertised. Handle count and message size are checked against the kernel ABI
before writing. Unsupported operations fail explicitly.

### Native framing v1 (implementation pending guest verification)

The native Channel adapter uses a 32-byte, little-endian header in each kernel
message. Fields in order: uint32 magic 0x314f4d43 (CMO1), uint16 version1,
uint16 flags (FIRST=1, LAST=2), uint64 message_id, uint32 total_bytes,
uint32 byte_offset, uint16 total_handles, uint16 handle_offset,
uint16 fragment_handles, uint16 reserved0. The remaining kernel message bytes
are payload. No capability value is serialized into these bytes.

Logical messages have at most8MiB and64 capabilities. Each fragment has at
most65504 payload bytes and16 actual kernel capability attachments. Message
IDs start at1 and advance strictly. Both offsets advance without gaps; header
totals remain identical across fragments. FIRST is required exactly once,
LAST exactly when both byte and handle totals are reached. Empty fragments,
unknown flags/version, nonzero reserved fields, inconsistent attachment counts,
overlapping/out-of-order fragments or interleaved messages fail the channel.
An incomplete message is never dispatched. Handles received before a failure
are owned and closed along with the connection.

The sender owns a FIFO of at most128 logical messages and16MiB of message
bytes. Writes enqueue under a lock; kernel I/O and delegate calls run on the
designated I/O sequence. WouldBlock arms actual writable readiness without
replaying successful capability transfers. Queue exhaustion reports a channel
error asynchronously. Read/write batches are bounded so another task can run.
Public shutdown rejects new writes immediately; cleanup removes native watches
and closes retained handles on the I/O sequence. This framing is a native
transport extension, not an upstream network protocol or a new kernel ABI.

## Process bootstrap

`PlatformChannelEndpoint::PrepareToPass` allocates a startup role, duplicates
the endpoint with transfer rights and includes it in native `LaunchOptions`.
The serialized switch contains the role, never a raw pointer or PID. The child
takes the role once through the native startup-capability API. Parent and child
then create their Mojo channel around the respective endpoints.

Bootstrap roles use the local application range 0x50000000..0x5fffffff.
Allocation checks every existing LaunchOptions entry, including other native
startup roles. The original endpoint remains owned by the caller until
ProcessLaunchAttempted; LaunchProcess duplicates the borrowed endpoint with its
actual rights and transfers that duplicate. Transfer and Duplicate rights are
required. Recovery validates the range, takes the role once and verifies the
native channel object before returning an endpoint. This adds no kernel ABI.

The prepared mojo_runtime probe covers bootstrap, normal/ipcz Channel messages,
large-message reassembly, capability batches, backpressure, concurrent writes,
queue limits, malformed input and shutdown. A separate public API stage uses
OutgoingInvitation/IncomingInvitation, message pipes and shared-buffer wrappers
in real child processes, including restart. None is considered verified until
the executable links, passes ELF audit and completes in QEMU.

## Verification

A native guest test must exchange normal and ipcz messages in both directions,
transfer read-only VMO capabilities, sustain bounded backpressure, shut down in
both directions and restart a child process. These checks precede content and
Blink; an Ozone window alone is not evidence of a working browser engine.
