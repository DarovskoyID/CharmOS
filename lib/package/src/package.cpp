#include <lib/crypto/ed25519.hpp>
#include <lib/crypto/sha512.hpp>
#include <lib/package/package.hpp>

namespace lib::package {

namespace pk = abi::package;
using lib::core::Result;
using lib::core::StringView;

const char* error_name(Error e) {
  switch (e) {
    case Error::Ok: return "ok";
    case Error::Truncated: return "truncated";
    case Error::BadMagic: return "bad magic";
    case Error::BadVersion: return "bad version";
    case Error::BadLayout: return "bad layout";
    case Error::BadPath: return "bad path";
    case Error::BadManifest: return "bad manifest";
    case Error::NoExec: return "no executable";
    case Error::Unsigned: return "unsigned";
    case Error::BadSignature: return "bad signature";
    case Error::UntrustedKey: return "untrusted key";
    case Error::BadHash: return "file hash mismatch";
  }
  return "?";
}

namespace {

bool fits(uint64_t off, uint64_t len, uint64_t size) { return off <= size && len <= size - off; }

bool valid_path(const char* p) {
  size_t n = 0;
  while (n < pk::kMaxPath && p[n] != '\0') ++n;
  if (n == 0 || n == pk::kMaxPath || p[0] == '/') return false;
  for (size_t i = n; i < pk::kMaxPath; ++i) {
    if (p[i] != '\0') return false;  // дополнено нулями
  }
  // Компоненты непустые, без «.» и «..».
  size_t start = 0;
  for (size_t i = 0; i <= n; ++i) {
    if (i == n || p[i] == '/') {
      size_t len = i - start;
      if (len == 0) return false;
      if (len == 1 && p[start] == '.') return false;
      if (len == 2 && p[start] == '.' && p[start + 1] == '.') return false;
      start = i + 1;
    }
  }
  return true;
}

void copy(char* dst, size_t cap, StringView s) {
  size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
  for (size_t i = 0; i < n; ++i) dst[i] = s[i];
  dst[n] = '\0';
}

Result<View, Error> parse(const uint8_t* data, uint64_t size, bool meta) {
  using R = Result<View, Error>;
  if (size < sizeof(pk::Header)) return R::err(Error::Truncated);
  View v = {};
  v.base = data;
  v.size = size;
  v.meta_only = meta;
  v.header = reinterpret_cast<const pk::Header*>(data);
  const pk::Header& h = *v.header;
  for (int i = 0; i < 4; ++i) {
    if (h.magic[i] != pk::kMagic[i]) return R::err(Error::BadMagic);
  }
  if (h.version != pk::kVersion) return R::err(Error::BadVersion);
  if (h.flags != 0 || h.reserved != 0 || h.file_count == 0 || h.file_count > pk::kMaxFiles ||
      h.manifest_size == 0 || h.manifest_size > pk::kMaxManifest || h.table_offset != sizeof(pk::Header)) {
    return R::err(Error::BadLayout);
  }
  uint64_t table_end = h.table_offset + uint64_t{h.file_count} * sizeof(pk::FileEntry);
  if (h.manifest_offset != table_end || h.data_offset < h.manifest_offset + h.manifest_size ||
      h.data_offset % 8 != 0 || h.signed_size != h.data_offset) {
    return R::err(Error::BadLayout);
  }
  if (!fits(0, h.data_offset, size)) return R::err(Error::Truncated);
  v.files = reinterpret_cast<const pk::FileEntry*>(data + h.table_offset);
  v.manifest_text = StringView(reinterpret_cast<const char*>(data + h.manifest_offset), h.manifest_size);
  // Подпись: в полном пакете — после данных, в package.sig — сразу за подписанной частью.
  if (h.signature_offset != 0) {
    uint64_t sig_at = meta ? h.data_offset : h.signature_offset;
    if (!meta && h.signature_offset < h.data_offset) return R::err(Error::BadLayout);
    if (!fits(sig_at, pk::kSignatureBlock, size)) return R::err(Error::Truncated);
    v.signature = data + sig_at;
    v.signer = data + sig_at + 64;
  }
  for (uint32_t i = 0; i < h.file_count; ++i) {
    const pk::FileEntry& f = v.files[i];
    if (!valid_path(f.path)) return R::err(Error::BadPath);
    if ((f.mode & ~pk::kModeExec) != 0 || f.reserved != 0) return R::err(Error::BadLayout);
    if (f.offset < h.data_offset || f.offset % 8 != 0) return R::err(Error::BadLayout);
    if (!meta) {
      uint64_t data_end = h.signature_offset != 0 ? h.signature_offset : size;
      if (!fits(f.offset, f.size, data_end)) return R::err(Error::Truncated);
    }
  }
  auto m = parse_manifest(v.manifest_text);
  if (m.is_err()) return R::err(m.error());
  v.manifest = m.value();
  const pk::FileEntry* exec = find(v, StringView(v.manifest.exec));
  if (exec == nullptr || (exec->mode & pk::kModeExec) == 0) return R::err(Error::NoExec);
  return R::ok(v);
}

// --- манифест -------------------------------------------------------------------------------

bool id_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; }

StringView trim(StringView s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

// "строка" в начале s; остаток — в rest.
bool quoted(StringView s, StringView* out, StringView* rest) {
  if (s.size() < 2 || s[0] != '"') return false;
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i] == '\\') return false;  // экранирование не поддерживается
    if (s[i] == '"') {
      *out = s.substr(1, i - 1);
      *rest = trim(s.substr(i + 1, s.size() - i - 1));
      return true;
    }
  }
  return false;
}

bool strip_comment(StringView* rest) {
  if (rest->empty()) return true;
  return (*rest)[0] == '#';
}

}  // namespace

Result<Manifest, Error> parse_manifest(StringView text) {
  using R = Result<Manifest, Error>;
  Manifest m = {};
  bool have_id = false, have_name = false, have_version = false, have_exec = false, have_perms = false;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = pos;
    while (end < text.size() && text[end] != '\n') ++end;
    StringView line = trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == '#') continue;
    size_t eq = 0;
    while (eq < line.size() && line[eq] != '=') ++eq;
    if (eq == line.size()) return R::err(Error::BadManifest);
    StringView key = trim(line.substr(0, eq));
    StringView value = trim(line.substr(eq + 1, line.size() - eq - 1));
    StringView str, rest;
    if (key == StringView("permissions")) {
      if (have_perms || value.empty() || value[0] != '[') return R::err(Error::BadManifest);
      have_perms = true;
      value = trim(value.substr(1, value.size() - 1));
      while (!value.empty() && value[0] != ']') {
        if (!quoted(value, &str, &rest)) return R::err(Error::BadManifest);
        uint32_t bit = 0;
        if (str == StringView("net")) bit = pk::kPermNet;
        else if (str == StringView("net-listen")) bit = pk::kPermNetListen;
        else if (str == StringView("files")) bit = pk::kPermFiles;
        else if (str == StringView("home")) bit = pk::kPermHome;
        else if (str == StringView("console")) bit = pk::kPermConsole;
        else return R::err(Error::BadManifest);
        if ((m.permissions & bit) != 0) return R::err(Error::BadManifest);
        m.permissions |= bit;
        value = rest;
        if (!value.empty() && value[0] == ',') value = trim(value.substr(1, value.size() - 1));
      }
      if (value.empty()) return R::err(Error::BadManifest);
      rest = trim(value.substr(1, value.size() - 1));
      if (!strip_comment(&rest)) return R::err(Error::BadManifest);
      continue;
    }
    if (!quoted(value, &str, &rest) || !strip_comment(&rest)) return R::err(Error::BadManifest);
    if (key == StringView("id")) {
      if (have_id || str.empty() || str.size() > 64) return R::err(Error::BadManifest);
      for (size_t i = 0; i < str.size(); ++i) {
        if (!id_char(str[i])) return R::err(Error::BadManifest);
      }
      copy(m.id, sizeof m.id, str);
      have_id = true;
    } else if (key == StringView("name")) {
      if (have_name || str.empty() || str.size() > 64) return R::err(Error::BadManifest);
      copy(m.name, sizeof m.name, str);
      have_name = true;
    } else if (key == StringView("version")) {
      if (have_version || str.empty() || str.size() > 32) return R::err(Error::BadManifest);
      copy(m.version, sizeof m.version, str);
      have_version = true;
    } else if (key == StringView("exec")) {
      if (have_exec || str.empty() || str.size() >= pk::kMaxPath) return R::err(Error::BadManifest);
      copy(m.exec, sizeof m.exec, str);
      have_exec = true;
    } else {
      return R::err(Error::BadManifest);
    }
  }
  if (!have_id || !have_name || !have_version || !have_exec) return R::err(Error::BadManifest);
  return R::ok(m);
}

Result<View, Error> open(const uint8_t* data, uint64_t size) { return parse(data, size, false); }
Result<View, Error> open_meta(const uint8_t* data, uint64_t size) { return parse(data, size, true); }

const pk::FileEntry* find(const View& v, StringView path) {
  for (uint32_t i = 0; i < v.header->file_count; ++i) {
    if (StringView(v.files[i].path) == path) return &v.files[i];
  }
  return nullptr;
}

Error check_signature(const View& v, const uint8_t* trusted) {
  if (v.signature == nullptr) return Error::Unsigned;
  if (trusted != nullptr) {
    for (int i = 0; i < 32; ++i) {
      if (v.signer[i] != trusted[i]) return Error::UntrustedKey;
    }
  }
  uint8_t digest[64];
  lib::crypto::sha512(v.base, v.header->signed_size, digest);
  return lib::crypto::ed25519_verify(v.signer, digest, sizeof digest, v.signature) ? Error::Ok : Error::BadSignature;
}

bool check_file(const pk::FileEntry& f, const uint8_t* content, uint64_t size) {
  if (size != f.size) return false;
  uint8_t digest[64];
  lib::crypto::sha512(content, size, digest);
  for (int i = 0; i < 64; ++i) {
    if (digest[i] != f.sha512[i]) return false;
  }
  return true;
}

Error check_files(const View& v) {
  for (uint32_t i = 0; i < v.header->file_count; ++i) {
    const pk::FileEntry& f = v.files[i];
    if (!check_file(f, v.base + f.offset, f.size)) return Error::BadHash;
  }
  return Error::Ok;
}

}  // namespace lib::package
