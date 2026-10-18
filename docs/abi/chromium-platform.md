# Native Chromium platform: C++-only profile

M10 internal port; no new syscall ABI in this document. Native binaries use
CharmOS capabilities, own libc and the Generic ELF toolchain. Linux identity,
host libc, ELF dynamic loading and compiler TLS are not supported by this
profile. Missing platform operations must not report successful emulation.

The initial core target is //base, with real C++ JSONReader. Optional Rust
bindings, Rust logging registration and upstream test suites requiring those
bindings are excluded. Native probes remain separate runnable guest tests.
The newer ICU4X-based //base:i18n component is currently unavailable: a C++
ICU4C replacement is required before enabling clients of that component.
This is an unfinished browser port, not a reduced target claimed as a browser.

Perfetto must identify CharmOS separately, without defining Linux/FreeBSD or
other foreign OS flags. Use the embedded tracing client; system daemon IPC
is disabled until there is a native transport. Clock readings must use real
CharmOS clocks. Unsupported CPU clocks are not wall time measurements.

ICU can use its upstream stdio file-data backend rather than POSIX mmap.
Native paths and file contents remain UTF-8/bytes; data file load failures
must propagate normally. Unicode data must actually be loaded before claiming
locale services work.

Native libc calendar extension retains the existing 36-byte tm layout and
UTC-only Gregorian contract. localtime/localtime_r use UTC, tm_isdst is zero;
tzname entries are UTC, timezone and daylight are zero. tzset accepts no TZ,
empty TZ, UTC, UTC0 and GMT0. Other TZ values leave UTC selected and set
errno=EINVAL. This is a documented fixed-UTC subset, not a timezone database.
mktime normalizes civil components within Gregorian years 1..9999, rejects
positive tm_isdst, reports EOVERFLOW outside that range and writes the
normalized tm on success. difftime returns the difference in seconds.
These additions do not fabricate timezone support or CPU accounting.
