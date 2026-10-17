# Native bootstrap dependency notices

These are exact license files from Chromium 155.0.8059.84, pinned by
../upstream.json. manifest.json records each upstream path and SHA-256.
prepare-platform.py verifies both cached source and preserved copies.
The root CharmOS MIT license does not replace these texts.

The GN bootstrap compiles upstream libc++ algorithm.cpp/verbose_abort.cpp
and compiler-rt udivti3.c/umodti3.c/udivmodti4.c. The native TLS tests also
link the full BoringSSL crypto/ssl archives. Full-runtime bring-up compiles
additional libc++/libc++abi files but does not yet produce a working library.
Original copyright/SPDX notices remain in every source in the private cache;
all local modifications are expressed in the downstream preparation script.

Keep these notices alongside bootstrap binaries when distributing them.
This inventory covers the present bootstrap/TLS dependencies, not all third
party components of a future full Chromium browser; that requires Chromium's
complete component license inventory at packaging time.
