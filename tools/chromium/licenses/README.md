# Native bootstrap dependency notices

These are exact license files from Chromium 155.0.8059.84, pinned by
../upstream.json. manifest.json records each upstream path and SHA-256.
prepare-platform.py verifies both cached source and preserved copies.
The root CharmOS MIT license does not replace these texts.

The GN bootstrap compiles upstream libc++ algorithm.cpp/verbose_abort.cpp
and compiler-rt udivti3.c/umodti3.c/udivmodti4.c. The native TLS tests also
link the full BoringSSL crypto/ssl archives. Full-runtime bring-up compiles
additional libc++/libc++abi files and now passes the native runtime guest test.
Original copyright/SPDX notices remain in every source in the private cache;
all local modifications are expressed in the downstream preparation script.

Keep these notices alongside bootstrap binaries when distributing them.
This inventory covers the present bootstrap/TLS dependencies, not all third
party components of a future full Chromium browser; that requires Chromium's
complete component license inventory at packaging time.

The graphics profile additionally preserves Skia, libpng, libjpeg-turbo,
libwebp, Wuffs, Expat, HarfBuzz and FreeType notices. FreeType is used under
its FreeType Project License; the alternative GPL text is retained as part
of the upstream distribution, without replacing CharmOS's MIT license.
Portions of this software are copyright © 1996-2026 The FreeType Project
(https://freetype.org). All rights reserved.
The bundled Space Grotesk and JetBrains Mono assets retain their OFL notices
in assets/fonts and alongside the installed fonts in the graphics test image.

The native network profile uses QUICHE's existing C++ structured-header parser
instead of the optional Rust SFV bridge. Its BSD license is retained in
quiche.txt; no host-network implementation is substituted.
