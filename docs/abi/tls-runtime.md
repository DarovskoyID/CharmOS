# Native TLS browser dependency

Pinned BoringSSL from Chromium 155.0.8059.84, Generic x86-64 ELF,
OPENSSL_NO_ASM / OPENSSL_NO_SOCK. No Linux libc or host TLS in the guest.
Memory BIO separates the TLS engine from CharmOS socket transport. C++ ABI
support is documented in cxx-runtime.md; entropy and UTC in time-random.md.
Two bundled libc++ implementation files and three LLVM compiler-rt unsigned
128-bit division files are linked from the same source archive, with licenses
preserved in that source tree. This is not the complete libc++ library.

The browser test phase is optional and requires build-tls.sh followed by
build-tls-smoke.sh through with-cache.sh after building the native OS.
It is not required for a source-only checkout without the Chromium archive.

tls: client/server memory BIO, SHA-256 known vector, native RAND_bytes,
verified TLS 1.2/1.3 HTTP request/response, wrong hostname, untrusted issuer,
expired and not-yet-valid certificates. No external network in this case.

https: real guest TCP through virtio-net/slirp to a controlled host TLS
server, address 10.0.2.2:17443, certificate hostname charmos.test. Identical
positive/negative verification cases. No DNS lookup or internet/Yandex claim.
The ephemeral test CA/server keys are generated under ignored build/, never
installed as production trust roots. The guest clock is pinned to 2030;
fixtures generated in the current 2026 environment are valid for ten years.
The host verifies two actual HTTP requests and four rejected sessions.

The HTTPS proof is not a Chromium window. Browser process/IPC/Ozone, HTML/JS,
public trust roots, DNS, user settings, extensions and PDFium remain separate.
