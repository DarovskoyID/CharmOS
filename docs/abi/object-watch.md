# Native cancellable readiness watches — syscall ABI 1.14

This additive extension supports native browser event loops. Existing syscall
numbers, packet layouts and ObjectBind behavior remain unchanged.

73 ObjectUnbind(port, object, key) cancels an armed one-shot watch on a channel
endpoint, socket or console only when both the destination port and key match.
It releases the port reference and capacity reservation. Already delivered
packets remain in the port; an already fired/missing/mismatched watch returns
NotFound. Invalid handles/types/rights fail normally. A caller can therefore
cancel all outstanding subscriptions before destroying a temporary wait port.

74 ObjectWatch(port, object, key, mask) arms the existing one-shot subscription
with a readiness mask: bit 0 readable, bit 1 writable. Zero or unknown masks
are InvalidArgs. Channels and consoles support readable only. Sockets support
both and report the selected currently ready bits in PortPacket.data0;
packet.type remains SocketReadable when readable, otherwise SocketWritable.
Legacy ObjectBind continues to report its original packet types/data.
ObjectWatch performs an initial socket readiness sample before returning.
For a channel whose peer is already closed, ObjectWatch reports PeerClosed
even if messages remain; data0 bit 0 then indicates unread queued messages.
Legacy ObjectBind retains its queue-before-closure packet precedence.
Readability includes stream end-of-input. The native socket poller retains
its existing 10 ms readiness sampling; this extension does not claim edge
triggering or immediate network wakeups. Only one armed watch per object is
supported (Busy if occupied). A pending packet does not reserve a new watch.

libc poll uses temporary native ports with explicit cancellation. It never
holds the descriptor table lock while waiting. The first implementation is
limited to 64 descriptors per call, matching native port capacity; oversize
calls fail EINVAL. Unsupported readiness types fail ENOTSUP. Negative fds
are ignored, invalid positive fds report POLLNVAL, timeout uses monotonic time,
and signal interruption reports EINTR after cancellation.

ABI 1.16 additionally permits cancellation of legacy Process/Thread/Job binds.
Their existing one-shot notification packets and rights checks are unchanged;
only a matching armed port/object/key can be cancelled. SDK process waits use
this to release temporary ports after deadlines/interruption. Already queued
exit packets remain in the port and do not reserve a new subscription.
# ABI 1.18 extension

Channel endpoints also accept Writable and Readable|Writable, with packet 11
for writable readiness and shutdown-aware wakeups. Legacy mask-zero ObjectBind
keeps readable/peer-close behavior. See channel-control.md for the full contract.
