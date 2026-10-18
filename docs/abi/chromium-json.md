# Chromium JSON C++ backend

Internal source interface, not a new syscall ABI. Replaces Chromium 155's
Rust JSONReader implementation for the native C++-only CharmOS build.

Keep upstream JSONReader public signatures, Value types and option bits.
Accept one complete UTF-8 JSON value, optional leading BOM; reject trailing
input, non-finite numbers, malformed grammar and excess nesting. Preserve
int32 where representable; other numbers are finite doubles. Duplicate keys
use the last value. Strings decode JSON escapes and UTF-16 surrogate pairs;
unpaired surrogates and invalid UTF-8 fail unless replacement is requested.
Replacement produces U+FFFD. Unescaped CR/LF, comments, trailing commas,
vertical-tab escapes and Unicode \\xNN escapes require their respective flags.
Errors carry a nonempty diagnostic and one-based line and column positions.
Depth is bounded by both the caller's limit and Chromium's absolute limit.

Logging remains the existing C++ logging implementation. No Rust producers
exist in this profile, so its Rust registration bridge is omitted.

Validation: positive and negative parser fixtures, option isolation, Unicode,
numeric boundaries, truncation and nesting; then native //base integration.
