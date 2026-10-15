#include <lib/fs/gpt.hpp>

namespace lib::fs {

namespace {

constexpr uint64_t kSector = 512;
constexpr uint32_t kEntrySize = 128;
constexpr uint32_t kEntryCount = 128;  // массив 16 КиБ = 32 сектора
constexpr uint64_t kArraySectors = kEntryCount * kEntrySize / kSector;

template <typename T>
T get(const uint8_t* p) {
  T v;
  __builtin_memcpy(&v, p, sizeof v);
  return v;
}
template <typename T>
void put(uint8_t* p, T v) {
  __builtin_memcpy(p, &v, sizeof v);
}

}  // namespace

const char* error_name(FsError e) {
  switch (e) {
    case FsError::IoError: return "I/O error";
    case FsError::NotFound: return "not found";
    case FsError::AlreadyExists: return "already exists";
    case FsError::NoSpace: return "no space";
    case FsError::NotDirectory: return "not a directory";
    case FsError::IsDirectory: return "is a directory";
    case FsError::NotEmpty: return "directory not empty";
    case FsError::InvalidName: return "invalid name";
    case FsError::ReadOnly: return "read-only";
    case FsError::Corrupt: return "corrupt";
    case FsError::Unsupported: return "unsupported";
    case FsError::TooLarge: return "too large";
  }
  return "unknown";
}

bool valid_name(lib::core::StringView name) {
  if (name.empty() || name.size() > kMaxName) return false;
  if (name == lib::core::StringView(".") || name == lib::core::StringView("..")) return false;
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '/' || name[i] == '\0') return false;
  }
  return true;
}

uint32_t crc32(const void* data, size_t len, uint32_t crc) {
  const auto* p = static_cast<const uint8_t*>(data);
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

bool operator==(const Guid& a, const Guid& b) { return __builtin_memcmp(a.bytes, b.bytes, 16) == 0; }

// GUID на диске (EFI_GUID): первые три поля little-endian.
const Guid kGuidEsp = {{0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0,
                        0xc9, 0x3e, 0xc9, 0x3b}};
const Guid kGuidLinuxData = {{0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, 0x8e, 0x79, 0x3d,
                              0x69, 0xd8, 0x47, 0x7d, 0xe4}};

const Guid kGuidSystemArea = {{0x0e, 0x2f, 0xa1, 0xc4, 0x3b, 0x6a, 0x52, 0x4d, 0x9e, 0x3a, 0x43,
                               0x48, 0x41, 0x52, 0x53, 0x4f}};

Status read_gpt(Io& disk, GptInfo* out) {
  uint8_t h[kSector];
  if (disk.read(kSector, h, kSector).is_err()) return fail(FsError::IoError);
  if (__builtin_memcmp(h, "EFI PART", 8) != 0) return fail(FsError::Corrupt);
  uint32_t hsize = get<uint32_t>(h + 12);
  if (hsize < 92 || hsize > kSector) return fail(FsError::Corrupt);
  uint32_t want = get<uint32_t>(h + 16);
  put<uint32_t>(h + 16, 0);
  if (crc32(h, hsize) != want) return fail(FsError::Corrupt);
  uint64_t array_lba = get<uint64_t>(h + 72);
  uint32_t count = get<uint32_t>(h + 80);
  uint32_t esize = get<uint32_t>(h + 84);
  uint32_t array_crc = get<uint32_t>(h + 88);
  if (esize < 128 || esize % 128 != 0 || count == 0 || count > 1024) return fail(FsError::Corrupt);
  uint64_t first_usable = get<uint64_t>(h + 40);
  uint64_t last_usable = get<uint64_t>(h + 48);

  out->count = 0;
  uint32_t crc = 0;
  uint8_t e[kSector];
  uint64_t bytes = uint64_t{count} * esize;
  for (uint64_t off = 0; off < bytes; off += kSector) {
    size_t chunk = bytes - off < kSector ? static_cast<size_t>(bytes - off) : kSector;
    if (disk.read(array_lba * kSector + off, e, chunk).is_err()) return fail(FsError::IoError);
    crc = crc32(e, chunk, crc);
    for (size_t k = 0; k + esize <= chunk; k += esize) {
      Guid type;
      __builtin_memcpy(type.bytes, e + k, 16);
      static constexpr Guid kZero = {};
      if (type == kZero) continue;
      GptPartition p;
      p.type = type;
      __builtin_memcpy(p.unique.bytes, e + k + 16, 16);
      p.first_lba = get<uint64_t>(e + k + 32);
      p.last_lba = get<uint64_t>(e + k + 40);
      if (p.first_lba < first_usable || p.last_lba > last_usable || p.last_lba < p.first_lba) {
        return fail(FsError::Corrupt);
      }
      if (out->count < GptInfo::kMax) out->parts[out->count++] = p;
    }
  }
  if (crc != array_crc) return fail(FsError::Corrupt);
  return ok();
}

Status write_gpt(Io& disk, const GptPartition* parts, size_t count, const Guid& disk_guid) {
  uint64_t sectors = disk.size() / kSector;
  if (count > kEntryCount || sectors < 2 * (2 + kArraySectors) + 1) return fail(FsError::NoSpace);
  uint64_t first_usable = 2 + kArraySectors;
  uint64_t last_usable = sectors - 2 - kArraySectors;
  for (size_t i = 0; i < count; ++i) {
    if (parts[i].first_lba < first_usable || parts[i].last_lba > last_usable ||
        parts[i].last_lba < parts[i].first_lba) {
      return fail(FsError::NoSpace);
    }
  }

  // Защитный MBR (табл. 5.3–5.4).
  uint8_t mbr[kSector] = {};
  uint8_t* rec = mbr + 446;
  rec[2] = 0x02;  // StartingCHS = 0x000200
  rec[4] = 0xee;
  rec[5] = rec[6] = rec[7] = 0xff;
  put<uint32_t>(rec + 8, 1);
  put<uint32_t>(rec + 12, sectors - 1 > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(sectors - 1));
  mbr[510] = 0x55;
  mbr[511] = 0xaa;
  if (disk.write(0, mbr, kSector).is_err()) return fail(FsError::IoError);

  // Массив записей (табл. 5.6): одинаковый для основной и резервной копии.
  uint32_t array_crc = 0;
  for (uint64_t s = 0; s < kArraySectors; ++s) {
    uint8_t e[kSector] = {};
    for (uint32_t k = 0; k < kSector / kEntrySize; ++k) {
      size_t i = s * (kSector / kEntrySize) + k;
      if (i >= count) break;
      uint8_t* p = e + k * kEntrySize;
      __builtin_memcpy(p, parts[i].type.bytes, 16);
      __builtin_memcpy(p + 16, parts[i].unique.bytes, 16);
      put<uint64_t>(p + 32, parts[i].first_lba);
      put<uint64_t>(p + 40, parts[i].last_lba);
    }
    array_crc = crc32(e, kSector, array_crc);
    if (disk.write((2 + s) * kSector, e, kSector).is_err() ||
        disk.write((last_usable + 1 + s) * kSector, e, kSector).is_err()) {
      return fail(FsError::IoError);
    }
  }

  // Заголовки (табл. 5.5): основной в LBA 1, резервный в последнем секторе.
  for (int backup = 0; backup < 2; ++backup) {
    uint8_t h[kSector] = {};
    __builtin_memcpy(h, "EFI PART", 8);
    put<uint32_t>(h + 8, 0x00010000);
    put<uint32_t>(h + 12, 92);
    uint64_t my = backup ? sectors - 1 : 1;
    put<uint64_t>(h + 24, my);
    put<uint64_t>(h + 32, backup ? 1 : sectors - 1);
    put<uint64_t>(h + 40, first_usable);
    put<uint64_t>(h + 48, last_usable);
    __builtin_memcpy(h + 56, disk_guid.bytes, 16);
    put<uint64_t>(h + 72, backup ? last_usable + 1 : 2);
    put<uint32_t>(h + 80, kEntryCount);
    put<uint32_t>(h + 84, kEntrySize);
    put<uint32_t>(h + 88, array_crc);
    put<uint32_t>(h + 16, crc32(h, 92));
    if (disk.write(my * kSector, h, kSector).is_err()) return fail(FsError::IoError);
  }
  return disk.flush();
}

}  // namespace lib::fs
