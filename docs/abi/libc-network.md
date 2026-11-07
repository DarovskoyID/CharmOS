# Native libc network adapter v2

Status: extension under implementation for the native Chromium port. The
kernel socket ABI remains the IPv4 capability ABI in socket.md; these local
libc types do not add IPv6 packets, interfaces or socket options to the kernel.
Existing sockaddr, sockaddr_in and their field offsets remain unchanged.

Sources saved before implementation: docs/specs/rfc3493.txt (sections3,5,6),
docs/specs/rfc5952.txt (canonical IPv6 text). Source URLs are
https://www.rfc-editor.org/rfc/rfc3493.txt and
https://www.rfc-editor.org/rfc/rfc5952.txt. The RFCs retain their own notices.

The v2 local adapter adds in6_addr (16 address bytes), sockaddr_in6 (28 bytes:
uint16 family/port, uint32 flowinfo,16 address bytes,uint32 scope_id), and
sockaddr_storage (128 bytes with uint16 family prefix and8-byte alignment).
IPv6 port/flowinfo/address are network byte order; scope_id is host byte order.
No local sockaddr structure is copied unvalidated into a kernel ABI request.

The independent native SDK header charm/network.hpp exposes NetworkInfo and
network_info(Net, out), plus resolve_ipv4(Net, name, length, deadline). NetworkInfo
is the existing32-byte NetInfo record from socket.md, including unchanged
network-order addresses, MAC, MTU and link flag. These helpers borrow the Net
capability and return real syscall statuses; they introduce no new syscall or
host dependency. They can be used by other browser engines without libc or
Chromium. The libc adapter maps the same snapshot to POSIX types and errno.

inet_pton supports strict four-component decimal IPv4 and full RFC3493 IPv6,
including a single :: compression and an embedded IPv4 tail. Invalid input
returns0 and leaves destination bytes unchanged. Unsupported families return
-1/EAFNOSUPPORT. IPv4 components must have1..3 decimal digits and no ambiguous
leading zeros. IPv6 zone suffixes belong to the resolver, not inet_pton.

inet_ntop emits decimal IPv4 and canonical lowercase IPv6 with the longest
zero run compressed (first run on ties; never a single zero group). IPv4-mapped
IPv6 uses the ::ffff:a.b.c.d form. Too-short output returns nullptr/ENOSPC
without a partial result. Null arguments fail EFAULT. The pure conversion
functions do not need a Net capability.

Resolver wrappers must use the actual Net capability and DnsResolve syscall;
they must not query a host OS, invent DNS success, or replace failed queries
with a guessed address. Numeric addresses require no DNS capability. Kernel
IPv6 socket operations remain explicitly unsupported until implemented.

getaddrinfo owns a linked allocation per result; freeaddrinfo releases the
whole list. Numeric inputs, localhost and numeric/common service names are
handled locally. DNS names use the real IPv4 resolver and require NetResolve.
AI_ADDRCONFIG queries the actual NetInfo IPv4 configuration; it does not
invent an IPv6 interface. AI_CANONNAME is supported for local/numeric inputs;
arbitrary DNS canonical names are rejected until the resolver can expose CNAME
metadata. getnameinfo supports numeric formatting and named common services;
without reverse DNS it uses the standard numeric fallback, or EAI_NONAME if
NI_NAMEREQD forbids that fallback. Unsupported flags never pretend to work.

Nonblocking connect, local/peer address queries and socket options require
actual connection state from the kernel. Declaring their interfaces is not
proof that they work. No zero SO_ERROR or invented endpoint may substitute
for an unimplemented query.

## Local adapter additions (2026-11-04)

Interface snapshots use native NetInfo. The single Ethernet interface is
named net0 with index1, present only when the actual kernel reports link,
IPv4 configuration or MTU. No loopback interface is fabricated. Addresses,
mask, broadcast and resolver DNS server derive from the same kernel data.
No Net capability returns EACCES without replacing caller output.
FIONBIO changes the shared description's real nonblocking flag. FIONREAD
returns ENOTSUP until the kernel exposes a byte count, preserving output.
Message scatter/gather currently returns ENOTSUP rather than splitting UDP
messages or ignoring flags. Thread naming likewise returns ENOTSUP.
select has the same64 simultaneous-watch capacity as poll, with validated
nonnegative timevals, rounded-up millisecond waits and EBADF on invalid FD.
