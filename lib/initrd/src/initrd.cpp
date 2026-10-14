#include <lib/initrd/initrd.hpp>

namespace lib::initrd {

using abi::initrd::Entry;
using abi::initrd::Header;
using lib::core::Status;
using lib::core::StringView;

namespace {

Status<InitrdError> fail(InitrdError e) { return Status<InitrdError>::err(e); }

StringView entry_name(const Entry& e) {
  size_t n = 0;
  while (n < abi::initrd::kNameSize && e.name[n] != '\0') ++n;
  return StringView(e.name, n);
}

}  // namespace

const char* error_name(InitrdError e) {
  switch (e) {
    case InitrdError::TooSmall: return "archive too small";
    case InitrdError::BadMagic: return "bad magic";
    case InitrdError::BadVersion: return "unsupported version";
    case InitrdError::BadSize: return "total_size larger than provided";
    case InitrdError::BadEntry: return "entry out of bounds or malformed";
    case InitrdError::DuplicateName: return "duplicate file name";
  }
  return "unknown";
}

Status<InitrdError> Archive::open(const void* data, uint64_t size) {
  if (size < sizeof(Header)) return fail(InitrdError::TooSmall);
  const auto* h = static_cast<const Header*>(data);
  for (size_t i = 0; i < sizeof h->magic; ++i) {
    if (h->magic[i] != abi::initrd::kMagic[i]) return fail(InitrdError::BadMagic);
  }
  if (h->version != abi::initrd::kVersion) return fail(InitrdError::BadVersion);
  if (h->total_size > size || h->total_size < sizeof(Header)) return fail(InitrdError::BadSize);

  uint64_t table_end = sizeof(Header) + uint64_t{h->entry_count} * sizeof(Entry);
  if (table_end > h->total_size) return fail(InitrdError::BadEntry);
  const auto* entries = reinterpret_cast<const Entry*>(static_cast<const uint8_t*>(data) +
                                                       sizeof(Header));
  for (uint32_t i = 0; i < h->entry_count; ++i) {
    const Entry& e = entries[i];
    StringView name = entry_name(e);
    if (name.empty() || name.size() == abi::initrd::kNameSize) return fail(InitrdError::BadEntry);
    if (e.offset % abi::initrd::kDataAlign != 0 || e.offset < table_end) {
      return fail(InitrdError::BadEntry);
    }
    uint64_t end = e.offset + e.size;
    if (end < e.offset || end > h->total_size) return fail(InitrdError::BadEntry);
    for (uint32_t j = 0; j < i; ++j) {
      if (entry_name(entries[j]) == name) return fail(InitrdError::DuplicateName);
    }
  }
  base_ = static_cast<const uint8_t*>(data);
  entries_ = entries;
  count_ = h->entry_count;
  return lib::core::ok_status<InitrdError>();
}

File Archive::file(uint32_t index) const {
  const Entry& e = entries_[index];
  return {entry_name(e), base_ + e.offset, e.size};
}

bool Archive::find(StringView name, File* out) const {
  for (uint32_t i = 0; i < count_; ++i) {
    if (entry_name(entries_[i]) == name) {
      *out = file(i);
      return true;
    }
  }
  return false;
}

}  // namespace lib::initrd
