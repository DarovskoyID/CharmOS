# Channel control — syscall ABI 1.18

Additive extension of ipc.md and object-watch.md. Existing channel messages,
capability transfer, queue limits and syscall numbers retain their meaning.
These operations expose native channel state, not Unix socket identifiers.

ChannelQuery=81 takes endpoint RDI and ChannelState* RSI. It requires Read
and snapshots that endpoint's queued payload bytes (including messages that
also contain handles), message count and shutdown flags under the IPC lock.
The fixed 16-byte layout is uint64 queued_bytes at offset 0, uint32 messages
at 8 and uint32 flags at 12. Flags: local Read shut=1, local Write shut=2,
peer Write shut/absent=4, peer Read shut/absent=8. This does not consume data
or transfer authority. Unknown handle/type, missing rights and invalid output
use existing BadHandle/WrongType/AccessDenied/BadAddress statuses.
Endpoint type is checked before Read permission, allowing the libc readiness
bridge to validate a write-only endpoint without inspecting its incoming queue.

ChannelShutdown=82 takes endpoint RDI and directions RSI, a nonzero subset of
Read=1/Write=2. Required rights are the corresponding Read/Write bits. Shutdown
is irreversible, idempotent and applies to the endpoint object and duplicates,
without closing/reusing the caller's handle. Read shutdown discards queued
incoming messages (closing their still-transferred capabilities), wakes local
readers and causes local reads and peer writes to return PeerClosed. Write
shutdown prevents local writes, wakes peer readers and allows the peer to drain
already queued incoming messages before reading PeerClosed. No partially
transferred message is reported as successful. All queue operations share the
IPC lock. Internal startup sends obey shutdown too. Shutdown does not revoke
already received handles or VMO mappings.

ObjectWatch now accepts Write=2 and Read|Write=3 for channel endpoints. The
existing single one-shot subscription per endpoint still applies. A writable
endpoint has a live peer accepting reads, local writes enabled and peer queue
below the existing 64-message ceiling. Consuming a message wakes a pending
writable watch. New packet type ChannelWritable=11; Readable=2 and PeerClosed=3
retain their values. Combined watches prioritize readable data over writable
readiness. Relevant shutdown/peer closure wakes watchers to retry the operation
and observe PeerClosed. Legacy ObjectBind mask zero remains the existing
readable/peer-closed subscription; it does not subscribe to writes. Unbind
retains matching endpoint/port/key cancellation and reservation cleanup.

SDK stream adapters use these operations without polling timers: sends wait
on writable readiness and retry with the original deadline; partial message
reads stay in a bounded receiving buffer. Each adapter owns its endpoint and
must be quiescent with no buffered data before transferring ownership. Shutdown
cancels concurrent blocking I/O without closing a handle underneath a thread.
