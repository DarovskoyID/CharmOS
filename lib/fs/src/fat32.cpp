#include <lib/fs/fat32.hpp>

namespace lib::fs {

using lib::core::StringView;

namespace {

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

constexpr uint32_t kEntry = 32;
constexpr uint32_t kMask = 0x0fffffff;  // 28 бит записи FAT32
constexpr uint32_t kEoc = 0x0fffffff;
constexpr uint32_t kEocMin = 0x0ffffff8;
constexpr uint32_t kBad = 0x0ffffff7;
constexpr uint8_t kFree = 0xe5;
constexpr uint8_t kAttrReadOnly = 0x01;
constexpr uint8_t kAttrHidden = 0x02;
constexpr uint8_t kAttrSystem = 0x04;
constexpr uint8_t kAttrVolumeId = 0x08;
constexpr uint8_t kAttrDirectory = 0x10;
constexpr uint8_t kAttrArchive = 0x20;
constexpr uint8_t kAttrLongName = kAttrReadOnly | kAttrHidden | kAttrSystem | kAttrVolumeId;
constexpr uint8_t kAttrLongNameMask = kAttrLongName | kAttrDirectory | kAttrArchive;
constexpr uint8_t kLastLong = 0x40;
constexpr uint16_t kDate1980 = (0 << 9) | (1 << 5) | 1;  // 1980-01-01
constexpr uint32_t kLfnChars = 13;
constexpr uint32_t kMaxLfnChars = 255;

// Позиции символов UCS-2 в длинной записи: Name1 (1, 5), Name2 (14, 6), Name3 (28, 2).
constexpr uint8_t kLfnPos[kLfnChars] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

// Контрольная сумма короткого имени (fatgen103, ChkSum).
uint8_t short_checksum(const uint8_t* name) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; ++i) {
    sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i]);
  }
  return sum;
}

char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; }

bool same_name(StringView a, StringView b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (upper(a[i]) != upper(b[i])) return false;
  }
  return true;
}

// UTF-8 → UCS-2 (только BMP). false — недопустимая последовательность.
bool to_ucs2(StringView s, uint16_t* out, uint32_t* n) {
  *n = 0;
  for (size_t i = 0; i < s.size();) {
    auto b = static_cast<uint8_t>(s[i]);
    uint32_t cp, len;
    if (b < 0x80) {
      cp = b, len = 1;
    } else if ((b & 0xe0) == 0xc0) {
      cp = b & 0x1f, len = 2;
    } else if ((b & 0xf0) == 0xe0) {
      cp = b & 0x0f, len = 3;
    } else {
      return false;
    }
    if (i + len > s.size()) return false;
    for (uint32_t k = 1; k < len; ++k) {
      auto c = static_cast<uint8_t>(s[i + k]);
      if ((c & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (c & 0x3f);
    }
    if (*n == kMaxLfnChars) return false;
    out[(*n)++] = static_cast<uint16_t>(cp);
    i += len;
  }
  return true;
}

// UCS-2 → UTF-8; false — не помещается в kMaxName.
bool to_utf8(const uint16_t* in, uint32_t n, char* out, uint8_t* len) {
  size_t k = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t c = in[i];
    size_t need = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
    if (k + need > kMaxName) return false;
    if (need == 1) {
      out[k++] = static_cast<char>(c);
    } else if (need == 2) {
      out[k++] = static_cast<char>(0xc0 | (c >> 6));
      out[k++] = static_cast<char>(0x80 | (c & 0x3f));
    } else {
      out[k++] = static_cast<char>(0xe0 | (c >> 12));
      out[k++] = static_cast<char>(0x80 | ((c >> 6) & 0x3f));
      out[k++] = static_cast<char>(0x80 | (c & 0x3f));
    }
  }
  out[k] = '\0';
  *len = static_cast<uint8_t>(k);
  return true;
}

// Символ, допустимый в коротком имени (fatgen103: буквы, цифры и $%'-_@~`!(){}^#&).
bool short_ok(char c) {
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
  const char* extra = "$%'-_@~`!(){}^#&";
  for (const char* p = extra; *p != '\0'; ++p) {
    if (c == *p) return true;
  }
  return false;
}

}  // namespace

struct Fat32::Raw {
  uint8_t name[11];
  uint8_t attr;
  uint8_t nt_res;
  uint8_t crt_time_tenth;
  uint16_t crt_time;
  uint16_t crt_date;
  uint16_t lst_acc_date;
  uint16_t fst_clus_hi;
  uint16_t wrt_time;
  uint16_t wrt_date;
  uint16_t fst_clus_lo;
  uint32_t file_size;

  uint32_t first() const { return (uint32_t{fst_clus_hi} << 16 | fst_clus_lo) & kMask; }
  void set_first(uint32_t c) {
    fst_clus_hi = static_cast<uint16_t>(c >> 16);
    fst_clus_lo = static_cast<uint16_t>(c);
  }
};
static_assert(sizeof(Fat32::Raw) == 32);
static_assert(__builtin_offsetof(Fat32::Raw, attr) == 11);
static_assert(__builtin_offsetof(Fat32::Raw, fst_clus_hi) == 20);
static_assert(__builtin_offsetof(Fat32::Raw, fst_clus_lo) == 26);
static_assert(__builtin_offsetof(Fat32::Raw, file_size) == 28);

struct Fat32::Found {
  uint64_t lfn_number;    // номер первой записи набора (длинной или короткой)
  uint64_t short_number;  // номер короткой записи
  uint64_t offset;        // смещение короткой записи
  Raw raw;
  DirEntry entry;
};

// --- монтирование ---------------------------------------------------------------------

Status Fat32::mount(Io* io) {
  io_ = io;
  uint8_t bs[512];
  if (io->read(0, bs, sizeof bs).is_err()) return fail(FsError::IoError);
  if (bs[510] != 0x55 || bs[511] != 0xaa) return fail(FsError::Corrupt);
  bytes_per_sector_ = get<uint16_t>(bs + 11);
  uint32_t spc = bs[13];
  uint32_t reserved = get<uint16_t>(bs + 14);
  num_fats_ = bs[16];
  uint32_t root_entries = get<uint16_t>(bs + 17);
  uint32_t tot16 = get<uint16_t>(bs + 19);
  uint32_t fatsz16 = get<uint16_t>(bs + 22);
  uint32_t tot32 = get<uint32_t>(bs + 32);
  uint32_t fatsz32 = get<uint32_t>(bs + 36);
  uint16_t ext_flags = get<uint16_t>(bs + 40);
  uint16_t version = get<uint16_t>(bs + 42);
  root_cluster_ = get<uint32_t>(bs + 44);
  fsinfo_sector_ = get<uint16_t>(bs + 48);
  bool bps_ok = bytes_per_sector_ == 512 || bytes_per_sector_ == 1024 ||
                bytes_per_sector_ == 2048 || bytes_per_sector_ == 4096;
  if (!bps_ok || spc == 0 || (spc & (spc - 1)) != 0 || reserved == 0 || num_fats_ == 0) {
    return fail(FsError::Corrupt);
  }
  // Тип — только по числу кластеров (fatgen103: FAT32 — от 65525).
  uint32_t fatsz = fatsz16 != 0 ? fatsz16 : fatsz32;
  uint32_t total = tot16 != 0 ? tot16 : tot32;
  uint32_t root_sectors = (root_entries * 32 + bytes_per_sector_ - 1) / bytes_per_sector_;
  uint64_t meta = uint64_t{reserved} + uint64_t{num_fats_} * fatsz + root_sectors;
  if (fatsz == 0 || meta >= total) return fail(FsError::Corrupt);
  clusters_ = static_cast<uint32_t>((total - meta) / spc);
  if (clusters_ < 65525) return fail(FsError::Unsupported);  // FAT12/16
  if (version != 0) return fail(FsError::Unsupported);  // fatgen103: старше 0.0 не монтировать
  if ((ext_flags & 0x80) != 0) return fail(FsError::Unsupported);  // FAT без зеркалирования
  if (uint64_t{total} * bytes_per_sector_ > io->size()) return fail(FsError::Corrupt);
  cluster_bytes_ = spc * bytes_per_sector_;
  fat_offset_ = uint64_t{reserved} * bytes_per_sector_;
  fat_bytes_ = uint64_t{fatsz} * bytes_per_sector_;
  if (fat_bytes_ < (uint64_t{clusters_} + 2) * 4) return fail(FsError::Corrupt);
  data_offset_ = meta * bytes_per_sector_;
  if (root_cluster_ < 2 || root_cluster_ >= clusters_ + 2) return fail(FsError::Corrupt);

  if (fsinfo_sector_ != 0 && fsinfo_sector_ < reserved) {
    uint8_t fi[512];
    if (io->read(uint64_t{fsinfo_sector_} * bytes_per_sector_, fi, sizeof fi).is_err()) {
      return fail(FsError::IoError);
    }
    if (get<uint32_t>(fi) == 0x41615252 && get<uint32_t>(fi + 484) == 0x61417272) {
      uint32_t hint = get<uint32_t>(fi + 492);
      if (hint >= 2 && hint < clusters_ + 2) next_free_ = hint;
    } else {
      fsinfo_sector_ = 0;
    }
  }
  auto free = free_clusters();
  if (free.is_err()) return fail(free.error());
  free_count_ = free.value();
  return ok();
}

// --- FAT --------------------------------------------------------------------------------

uint64_t Fat32::cluster_offset(uint32_t c) const {
  return data_offset_ + uint64_t{c - 2} * cluster_bytes_;
}

Result<uint32_t> Fat32::fat_get(uint32_t c) {
  if (c < 2 || c >= clusters_ + 2) return Result<uint32_t>::err(FsError::Corrupt);
  uint32_t v;
  if (io_->read(fat_offset_ + uint64_t{c} * 4, &v, 4).is_err()) {
    return Result<uint32_t>::err(FsError::IoError);
  }
  return Result<uint32_t>::ok(v & kMask);
}

Status Fat32::fat_set(uint32_t c, uint32_t v) {
  for (uint32_t f = 0; f < num_fats_; ++f) {
    uint64_t off = fat_offset_ + f * fat_bytes_ + uint64_t{c} * 4;
    uint32_t old;
    if (io_->read(off, &old, 4).is_err()) return fail(FsError::IoError);
    uint32_t nv = (old & ~kMask) | (v & kMask);  // старшие 4 бита сохраняются
    if (io_->write(off, &nv, 4).is_err()) return fail(FsError::IoError);
  }
  fsinfo_dirty_ = true;
  return ok();
}

Result<uint32_t> Fat32::alloc_cluster(bool zero) {
  uint32_t buf[128];
  for (uint32_t pass = 0; pass < 2; ++pass) {
    uint32_t start = pass == 0 ? next_free_ : 2;
    uint32_t end = pass == 0 ? clusters_ + 2 : next_free_;
    for (uint32_t c = start; c < end;) {
      uint32_t n = end - c < 128 ? end - c : 128;
      if (io_->read(fat_offset_ + uint64_t{c} * 4, buf, n * 4).is_err()) {
        return Result<uint32_t>::err(FsError::IoError);
      }
      for (uint32_t i = 0; i < n; ++i) {
        if ((buf[i] & kMask) != 0) continue;
        uint32_t found = c + i;
        if (fat_set(found, kEoc).is_err()) return Result<uint32_t>::err(FsError::IoError);
        --free_count_;
        next_free_ = found + 1 < clusters_ + 2 ? found + 1 : 2;
        if (zero) {
          uint8_t z[512] = {};
          for (uint32_t off = 0; off < cluster_bytes_; off += sizeof z) {
            if (io_->write(cluster_offset(found) + off, z, sizeof z).is_err()) {
              return Result<uint32_t>::err(FsError::IoError);
            }
          }
        }
        return Result<uint32_t>::ok(found);
      }
      c += n;
    }
  }
  return Result<uint32_t>::err(FsError::NoSpace);
}

Status Fat32::free_chain(uint32_t c) {
  for (uint32_t guard = 0; c != 0 && guard <= clusters_; ++guard) {
    auto next = fat_get(c);
    if (next.is_err()) return fail(next.error());
    if (fat_set(c, 0).is_err()) return fail(FsError::IoError);
    ++free_count_;
    if (c < next_free_) next_free_ = c;
    if (next.value() >= kEocMin) return ok();
    if (next.value() == kBad || next.value() < 2) return fail(FsError::Corrupt);
    c = next.value();
  }
  return c == 0 ? ok() : fail(FsError::Corrupt);
}

Result<uint32_t> Fat32::chain_at(uint32_t* first, uint64_t index, bool grow, bool zero) {
  if (*first == 0) {
    if (!grow) return Result<uint32_t>::err(FsError::Corrupt);
    auto c = alloc_cluster(zero);
    if (c.is_err()) return c;
    *first = c.value();
  }
  uint32_t c = *first;
  if (index > clusters_) return Result<uint32_t>::err(FsError::TooLarge);
  for (uint64_t i = 0; i < index; ++i) {
    auto next = fat_get(c);
    if (next.is_err()) return next;
    uint32_t n = next.value();
    if (n >= kEocMin) {
      if (!grow) return Result<uint32_t>::err(FsError::Corrupt);
      auto nc = alloc_cluster(zero);
      if (nc.is_err()) return nc;
      if (fat_set(c, nc.value()).is_err()) return Result<uint32_t>::err(FsError::IoError);
      n = nc.value();
    } else if (n < 2 || n == kBad) {
      return Result<uint32_t>::err(FsError::Corrupt);
    }
    c = n;
  }
  return Result<uint32_t>::ok(c);
}

Result<uint32_t> Fat32::free_clusters() {
  uint32_t count = 0, buf[128];
  for (uint32_t c = 2; c < clusters_ + 2;) {
    uint32_t n = clusters_ + 2 - c < 128 ? clusters_ + 2 - c : 128;
    if (io_->read(fat_offset_ + uint64_t{c} * 4, buf, n * 4).is_err()) {
      return Result<uint32_t>::err(FsError::IoError);
    }
    for (uint32_t i = 0; i < n; ++i) count += (buf[i] & kMask) == 0 ? 1 : 0;
    c += n;
  }
  return Result<uint32_t>::ok(count);
}

// --- каталоги ---------------------------------------------------------------------------

Status Fat32::read_raw(uint64_t off, Raw* out) {
  return io_->read(off, out, sizeof *out).is_err() ? fail(FsError::IoError) : ok();
}

Status Fat32::write_raw(uint64_t off, const Raw& r) {
  return io_->write(off, &r, sizeof r).is_err() ? fail(FsError::IoError) : ok();
}

Result<uint32_t> Fat32::dir_cluster(Node dir) {
  if (dir.id == 0) return Result<uint32_t>::ok(root_cluster_);
  Raw r;
  if (read_raw(dir.id, &r).is_err()) return Result<uint32_t>::err(FsError::IoError);
  if ((r.attr & kAttrDirectory) == 0) return Result<uint32_t>::err(FsError::NotDirectory);
  if (r.first() < 2) return Result<uint32_t>::err(FsError::Corrupt);
  return Result<uint32_t>::ok(r.first());
}

Status Fat32::set_first_cluster(Node node, uint32_t cluster) {
  Raw r;
  if (read_raw(node.id, &r).is_err()) return fail(FsError::IoError);
  r.set_first(cluster);
  return write_raw(node.id, r);
}

Result<uint64_t> Fat32::dir_entry_offset(uint32_t first, uint64_t number, bool grow) {
  uint64_t per = cluster_bytes_ / kEntry;
  uint32_t f = first;
  auto c = chain_at(&f, number / per, grow, true);
  if (c.is_err()) return Result<uint64_t>::err(c.error());
  return Result<uint64_t>::ok(cluster_offset(c.value()) + (number % per) * kEntry);
}

Status Fat32::scan(uint32_t dir_first, uint64_t from, Found* out, bool want_any) {
  (void)want_any;
  uint16_t lfn[kMaxLfnChars + kLfnChars];
  uint32_t lfn_expect = 0;  // следующий ожидаемый номер длинной записи, 0 — набора нет
  uint32_t lfn_total = 0;
  uint8_t lfn_sum = 0;
  uint64_t lfn_start = 0;
  uint64_t per = cluster_bytes_ / kEntry;
  uint32_t cluster = dir_first;
  // К кластеру номера from — по цепочке.
  {
    uint32_t f = dir_first;
    auto c = chain_at(&f, from / per, false, false);
    if (c.is_err()) return fail(c.error() == FsError::Corrupt ? FsError::NotFound : c.error());
    cluster = c.value();
  }
  for (uint64_t number = from;; ++number) {
    if (number != from && number % per == 0) {
      auto next = fat_get(cluster);
      if (next.is_err()) return fail(next.error());
      if (next.value() >= kEocMin) return fail(FsError::NotFound);
      if (next.value() < 2 || next.value() == kBad) return fail(FsError::Corrupt);
      cluster = next.value();
    }
    uint64_t off = cluster_offset(cluster) + (number % per) * kEntry;
    uint8_t e[kEntry];
    if (io_->read(off, e, kEntry).is_err()) return fail(FsError::IoError);
    if (e[0] == 0x00) return fail(FsError::NotFound);  // дальше записей нет
    if (e[0] == kFree) {
      lfn_expect = 0;
      continue;
    }
    uint8_t attr = e[11];
    if ((attr & kAttrLongNameMask) == kAttrLongName) {
      uint32_t ord = e[0] & 0x1f;
      if ((e[0] & kLastLong) != 0) {
        lfn_expect = ord;
        lfn_total = ord;
        lfn_sum = e[13];
        lfn_start = number;
        if (ord == 0 || ord * kLfnChars > kMaxLfnChars + kLfnChars) lfn_expect = 0;
      } else if (lfn_expect == 0 || ord != lfn_expect || e[13] != lfn_sum) {
        lfn_expect = 0;
        continue;
      }
      if (lfn_expect != 0) {
        for (uint32_t k = 0; k < kLfnChars; ++k) {
          lfn[(ord - 1) * kLfnChars + k] = get<uint16_t>(e + kLfnPos[k]);
        }
        --lfn_expect;
        if (lfn_expect == 0) lfn_expect = 0x100;  // набор собран: ждём короткую запись
      }
      continue;
    }
    bool have_lfn = lfn_expect == 0x100;
    lfn_expect = 0;
    if ((attr & kAttrVolumeId) != 0) continue;
    if (e[0] == '.' && (e[1] == ' ' || (e[1] == '.' && e[2] == ' '))) continue;  // «.» и «..»

    __builtin_memcpy(&out->raw, e, kEntry);
    out->short_number = number;
    out->offset = off;
    out->lfn_number = number;
    DirEntry& d = out->entry;
    d.node = {off, (attr & kAttrDirectory) != 0 ? NodeType::Directory : NodeType::File};
    bool named = false;
    if (have_lfn && short_checksum(e) == lfn_sum) {
      uint32_t n = 0;
      while (n < lfn_total * kLfnChars && lfn[n] != 0x0000) ++n;
      named = n != 0 && to_utf8(lfn, n, d.name, &d.name_len);
      if (named) out->lfn_number = lfn_start;
    }
    if (!named) {
      // Короткое имя: «ИМЯ.РАС» без пробелов (0x05 в первом байте — это 0xE5).
      size_t k = 0;
      for (int i = 0; i < 8 && e[i] != ' '; ++i) {
        d.name[k++] = static_cast<char>(i == 0 && e[0] == 0x05 ? 0xe5 : e[i]);
      }
      if (e[8] != ' ') {
        d.name[k++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; ++i) d.name[k++] = static_cast<char>(e[i]);
      }
      d.name[k] = '\0';
      d.name_len = static_cast<uint8_t>(k);
    }
    return ok();
  }
}

Status Fat32::find(uint32_t dir_first, StringView name, Found* out) {
  for (uint64_t from = 0;;) {
    Status s = scan(dir_first, from, out, false);
    if (s.is_err()) return s;
    if (same_name(StringView(out->entry.name, out->entry.name_len), name)) return ok();
    from = out->short_number + 1;
  }
}

Result<Node> Fat32::lookup(Node dir, StringView name) {
  auto first = dir_cluster(dir);
  if (first.is_err()) return Result<Node>::err(first.error());
  Found f;
  Status s = find(first.value(), name, &f);
  if (s.is_err()) return Result<Node>::err(s.error());
  return Result<Node>::ok(f.entry.node);
}

Status Fat32::read_dir(Node dir, uint64_t cookie, DirEntry* out, uint64_t* next) {
  auto first = dir_cluster(dir);
  if (first.is_err()) return fail(first.error());
  Found f;
  Status s = scan(first.value(), cookie, &f, true);
  if (s.is_err()) {
    if (s.error() != FsError::NotFound) return s;
    *next = 0;
    return ok();
  }
  *out = f.entry;
  *next = f.short_number + 1;
  return ok();
}

Result<Node> Fat32::create(Node dir, StringView name, NodeType type) {
  if (!valid_name(name)) return Result<Node>::err(FsError::InvalidName);
  auto first_r = dir_cluster(dir);
  if (first_r.is_err()) return Result<Node>::err(first_r.error());
  uint32_t dir_first = first_r.value();
  Found f;
  Status s = find(dir_first, name, &f);
  if (s.is_ok()) return Result<Node>::err(FsError::AlreadyExists);
  if (s.error() != FsError::NotFound) return Result<Node>::err(s.error());

  uint16_t ucs[kMaxLfnChars];
  uint32_t nchars = 0;
  if (!to_ucs2(name, ucs, &nchars) || nchars == 0) return Result<Node>::err(FsError::InvalidName);

  // Короткий псевдоним «ОСНОВА~N.РАС», уникальный в каталоге.
  uint8_t sn[11];
  __builtin_memset(sn, ' ', 11);
  size_t dot = name.size();
  for (size_t i = name.size(); i > 0; --i) {
    if (name[i - 1] == '.') {
      dot = i - 1;
      break;
    }
  }
  char base[8];
  size_t blen = 0;
  for (size_t i = 0; i < dot && blen < 6; ++i) {
    char c = upper(name[i]);
    if (c == ' ' || c == '.') continue;
    base[blen++] = short_ok(c) ? c : '_';
  }
  if (blen == 0) base[blen++] = '_';
  size_t e = 0;
  for (size_t i = dot + 1; i < name.size() && e < 3; ++i) {
    char c = upper(name[i]);
    if (c == ' ') continue;
    sn[8 + e++] = static_cast<uint8_t>(short_ok(c) ? c : '_');
  }
  bool unique = false;
  for (uint32_t n = 1; n < 1000000 && !unique; ++n) {
    char digits[8];
    size_t nd = 0;
    for (uint32_t v = n; v != 0; v /= 10) digits[nd++] = static_cast<char>('0' + v % 10);
    size_t keep = blen + 1 + nd <= 8 ? blen : 8 - 1 - nd;
    for (size_t i = 0; i < 8; ++i) sn[i] = ' ';
    for (size_t i = 0; i < keep; ++i) sn[i] = static_cast<uint8_t>(base[i]);
    sn[keep] = '~';
    for (size_t i = 0; i < nd; ++i) sn[keep + 1 + i] = static_cast<uint8_t>(digits[nd - 1 - i]);
    unique = true;
    for (uint64_t from = 0;;) {
      Found o;
      Status ss = scan(dir_first, from, &o, true);
      if (ss.is_err()) {
        if (ss.error() != FsError::NotFound) return Result<Node>::err(ss.error());
        break;
      }
      if (__builtin_memcmp(o.raw.name, sn, 11) == 0) {
        unique = false;
        break;
      }
      from = o.short_number + 1;
    }
  }
  if (!unique) return Result<Node>::err(FsError::NoSpace);

  // Место под набор: nlfn длинных записей и короткая подряд.
  uint32_t nlfn = (nchars + kLfnChars - 1) / kLfnChars;
  uint32_t need = nlfn + 1;
  uint64_t run_start = 0;
  uint32_t run = 0;
  for (uint64_t number = 0; run < need; ++number) {
    if (number >= 65536) return Result<Node>::err(FsError::NoSpace);  // fatgen103: до 65536 записей
    auto off = dir_entry_offset(dir_first, number, true);
    if (off.is_err()) return Result<Node>::err(off.error());
    uint8_t b;
    if (io_->read(off.value(), &b, 1).is_err()) return Result<Node>::err(FsError::IoError);
    if (b == 0x00 || b == kFree) {
      if (run == 0) run_start = number;
      ++run;
    } else {
      run = 0;
    }
  }

  // Каталог: свой кластер с «.» и «..».
  uint32_t cluster = 0;
  if (type == NodeType::Directory) {
    auto c = alloc_cluster(true);
    if (c.is_err()) return Result<Node>::err(c.error());
    cluster = c.value();
    Raw dots[2] = {};
    __builtin_memset(dots[0].name, ' ', 11);
    __builtin_memset(dots[1].name, ' ', 11);
    dots[0].name[0] = '.';
    dots[1].name[0] = dots[1].name[1] = '.';
    for (Raw& r : dots) {
      r.attr = kAttrDirectory;
      r.crt_date = r.wrt_date = r.lst_acc_date = kDate1980;
    }
    dots[0].set_first(cluster);
    dots[1].set_first(dir.id == 0 ? 0 : dir_first);  // родитель — корень: 0
    if (io_->write(cluster_offset(cluster), dots, sizeof dots).is_err()) {
      return Result<Node>::err(FsError::IoError);
    }
  }

  uint8_t sum = short_checksum(sn);
  for (uint32_t i = 0; i < nlfn; ++i) {
    uint32_t ord = nlfn - i;  // по порядку на диске: от последней части к первой
    uint8_t le[kEntry] = {};
    le[0] = static_cast<uint8_t>(ord | (i == 0 ? kLastLong : 0));
    le[11] = kAttrLongName;
    le[13] = sum;
    for (uint32_t k = 0; k < kLfnChars; ++k) {
      uint32_t idx = (ord - 1) * kLfnChars + k;
      uint16_t ch = idx < nchars ? ucs[idx] : idx == nchars ? 0x0000 : 0xffff;
      put<uint16_t>(le + kLfnPos[k], ch);
    }
    auto off = dir_entry_offset(dir_first, run_start + i, false);
    if (off.is_err() || io_->write(off.value(), le, kEntry).is_err()) {
      return Result<Node>::err(FsError::IoError);
    }
  }
  Raw r = {};
  __builtin_memcpy(r.name, sn, 11);
  r.attr = type == NodeType::Directory ? kAttrDirectory : kAttrArchive;
  r.crt_date = r.wrt_date = r.lst_acc_date = kDate1980;
  r.set_first(cluster);
  auto off = dir_entry_offset(dir_first, run_start + nlfn, false);
  if (off.is_err()) return Result<Node>::err(off.error());
  if (write_raw(off.value(), r).is_err()) return Result<Node>::err(FsError::IoError);
  return Result<Node>::ok(Node{off.value(), type});
}

Status Fat32::unlink(Node dir, StringView name) {
  auto first = dir_cluster(dir);
  if (first.is_err()) return fail(first.error());
  Found f;
  Status s = find(first.value(), name, &f);
  if (s.is_err()) return s;
  if ((f.raw.attr & kAttrDirectory) != 0) {
    Found inner;
    if (f.raw.first() < 2) return fail(FsError::Corrupt);
    Status e = scan(f.raw.first(), 0, &inner, true);
    if (e.is_ok()) return fail(FsError::NotEmpty);
    if (e.error() != FsError::NotFound) return e;
  }
  for (uint64_t n = f.lfn_number; n <= f.short_number; ++n) {
    auto off = dir_entry_offset(first.value(), n, false);
    if (off.is_err()) return fail(off.error());
    uint8_t mark = kFree;
    if (io_->write(off.value(), &mark, 1).is_err()) return fail(FsError::IoError);
  }
  return f.raw.first() != 0 ? free_chain(f.raw.first()) : ok();
}

// --- файлы ------------------------------------------------------------------------------

Result<Stat> Fat32::stat(Node node) {
  if (node.id == 0) return Result<Stat>::ok(Stat{0, NodeType::Directory, 0});
  Raw r;
  if (read_raw(node.id, &r).is_err()) return Result<Stat>::err(FsError::IoError);
  bool is_dir = (r.attr & kAttrDirectory) != 0;
  return Result<Stat>::ok(
      Stat{is_dir ? 0 : r.file_size, is_dir ? NodeType::Directory : NodeType::File, 0});
}

Result<size_t> Fat32::read(Node file, uint64_t offset, void* buf, size_t len) {
  if (file.id == 0) return Result<size_t>::err(FsError::IsDirectory);
  Raw r;
  if (read_raw(file.id, &r).is_err()) return Result<size_t>::err(FsError::IoError);
  if ((r.attr & kAttrDirectory) != 0) return Result<size_t>::err(FsError::IsDirectory);
  if (offset >= r.file_size) return Result<size_t>::ok(0);
  if (len > r.file_size - offset) len = static_cast<size_t>(r.file_size - offset);
  uint32_t first = r.first();
  auto c = chain_at(&first, offset / cluster_bytes_, false, false);
  if (c.is_err()) return Result<size_t>::err(c.error());
  uint32_t cluster = c.value();
  auto* out = static_cast<uint8_t*>(buf);
  size_t done = 0;
  while (done < len) {
    uint64_t in = (offset + done) % cluster_bytes_;
    size_t chunk = cluster_bytes_ - in < len - done ? static_cast<size_t>(cluster_bytes_ - in)
                                                    : len - done;
    if (io_->read(cluster_offset(cluster) + in, out + done, chunk).is_err()) {
      return Result<size_t>::err(FsError::IoError);
    }
    done += chunk;
    if (done < len) {
      auto next = fat_get(cluster);
      if (next.is_err()) return Result<size_t>::err(next.error());
      if (next.value() < 2 || next.value() >= kBad) return Result<size_t>::err(FsError::Corrupt);
      cluster = next.value();
    }
  }
  return Result<size_t>::ok(len);
}

Result<size_t> Fat32::write(Node file, uint64_t offset, const void* buf, size_t len) {
  if (file.id == 0) return Result<size_t>::err(FsError::IsDirectory);
  Raw r;
  if (read_raw(file.id, &r).is_err()) return Result<size_t>::err(FsError::IoError);
  if ((r.attr & kAttrDirectory) != 0) return Result<size_t>::err(FsError::IsDirectory);
  if (len == 0) return Result<size_t>::ok(0);
  if (offset + len > 0xffffffffull) return Result<size_t>::err(FsError::TooLarge);
  // Дыра между концом файла и offset — нулями.
  if (offset > r.file_size) {
    uint8_t z[512] = {};
    for (uint64_t pos = r.file_size; pos < offset;) {
      size_t chunk = offset - pos < sizeof z ? static_cast<size_t>(offset - pos) : sizeof z;
      auto w = write(file, pos, z, chunk);
      if (w.is_err()) return w;
      pos += chunk;
    }
    if (read_raw(file.id, &r).is_err()) return Result<size_t>::err(FsError::IoError);
  }
  uint32_t first = r.first();
  uint32_t old_first = first;
  auto c = chain_at(&first, offset / cluster_bytes_, true, false);
  if (c.is_err()) return Result<size_t>::err(c.error());
  uint32_t cluster = c.value();
  const auto* in = static_cast<const uint8_t*>(buf);
  size_t done = 0;
  while (done < len) {
    uint64_t pos = (offset + done) % cluster_bytes_;
    size_t chunk = cluster_bytes_ - pos < len - done ? static_cast<size_t>(cluster_bytes_ - pos)
                                                     : len - done;
    if (io_->write(cluster_offset(cluster) + pos, in + done, chunk).is_err()) {
      return Result<size_t>::err(FsError::IoError);
    }
    done += chunk;
    if (done < len) {
      uint32_t f = cluster;
      auto next = chain_at(&f, 1, true, false);
      if (next.is_err()) return Result<size_t>::err(next.error());
      cluster = next.value();
    }
  }
  if (read_raw(file.id, &r).is_err()) return Result<size_t>::err(FsError::IoError);
  if (first != old_first) r.set_first(first);
  if (offset + len > r.file_size) r.file_size = static_cast<uint32_t>(offset + len);
  r.wrt_date = kDate1980;
  if (write_raw(file.id, r).is_err()) return Result<size_t>::err(FsError::IoError);
  return Result<size_t>::ok(len);
}

Status Fat32::resize(Node file, uint64_t size) {
  if (file.id == 0) return fail(FsError::IsDirectory);
  Raw r;
  if (read_raw(file.id, &r).is_err()) return fail(FsError::IoError);
  if ((r.attr & kAttrDirectory) != 0) return fail(FsError::IsDirectory);
  if (size > 0xffffffffull) return fail(FsError::TooLarge);
  if (size > r.file_size) {
    uint8_t z[512] = {};
    for (uint64_t pos = r.file_size; pos < size;) {
      size_t chunk = size - pos < sizeof z ? static_cast<size_t>(size - pos) : sizeof z;
      auto w = write(file, pos, z, chunk);
      if (w.is_err()) return fail(w.error());
      pos += chunk;
    }
    return ok();
  }
  uint32_t first = r.first();
  if (size == 0) {
    if (first != 0 && free_chain(first).is_err()) return fail(FsError::Corrupt);
    r.set_first(0);
  } else if (first != 0) {
    auto last = chain_at(&first, (size - 1) / cluster_bytes_, false, false);
    if (last.is_err()) return fail(last.error());
    auto next = fat_get(last.value());
    if (next.is_err()) return fail(next.error());
    if (next.value() < kEocMin) {
      if (fat_set(last.value(), kEoc).is_err()) return fail(FsError::IoError);
      if (free_chain(next.value()).is_err()) return fail(FsError::Corrupt);
    }
  }
  r.file_size = static_cast<uint32_t>(size);
  return write_raw(file.id, r);
}

Status Fat32::sync() {
  if (fsinfo_dirty_ && fsinfo_sector_ != 0) {
    uint8_t fi[8];
    put<uint32_t>(fi, free_count_);
    put<uint32_t>(fi + 4, next_free_);
    if (io_->write(uint64_t{fsinfo_sector_} * bytes_per_sector_ + 488, fi, 8).is_err()) {
      return fail(FsError::IoError);
    }
    fsinfo_dirty_ = false;
  }
  return io_->flush().is_err() ? fail(FsError::IoError) : ok();
}

// --- форматирование ---------------------------------------------------------------------

Status Fat32::format(Io& io, uint32_t spc, const char* label) {
  constexpr uint32_t kBps = 512, kReserved = 32, kFats = 2;
  uint64_t total64 = io.size() / kBps;
  if (total64 > 0xffffffffull) return fail(FsError::TooLarge);
  auto total = static_cast<uint32_t>(total64);
  if (spc == 0 || (spc & (spc - 1)) != 0 || spc * kBps > 32 * 1024) return fail(FsError::Unsupported);
  uint32_t fatsz = 1, clusters = 0;
  for (int iter = 0; iter < 64; ++iter) {
    if (kReserved + kFats * fatsz >= total) return fail(FsError::NoSpace);
    clusters = (total - kReserved - kFats * fatsz) / spc;
    uint32_t need = static_cast<uint32_t>(((uint64_t{clusters} + 2) * 4 + kBps - 1) / kBps);
    if (need <= fatsz) break;
    fatsz = need;
  }
  if (clusters < 65525) return fail(FsError::NoSpace);  // меньше — уже не FAT32

  uint8_t z[4096] = {};
  uint64_t meta_bytes = (uint64_t{kReserved} + kFats * fatsz) * kBps;
  for (uint64_t off = 0; off < meta_bytes; off += sizeof z) {
    size_t chunk = meta_bytes - off < sizeof z ? static_cast<size_t>(meta_bytes - off) : sizeof z;
    if (io.write(off, z, chunk).is_err()) return fail(FsError::IoError);
  }
  uint64_t root_off = meta_bytes;  // кластер 2
  for (uint32_t off = 0; off < spc * kBps; off += kBps) {
    if (io.write(root_off + off, z, kBps).is_err()) return fail(FsError::IoError);
  }

  uint8_t bs[512] = {};
  bs[0] = 0xeb, bs[1] = 0x58, bs[2] = 0x90;
  __builtin_memcpy(bs + 3, "MSWIN4.1", 8);
  put<uint16_t>(bs + 11, kBps);
  bs[13] = static_cast<uint8_t>(spc);
  put<uint16_t>(bs + 14, kReserved);
  bs[16] = kFats;
  bs[21] = 0xf8;
  put<uint16_t>(bs + 24, 63);
  put<uint16_t>(bs + 26, 255);
  put<uint32_t>(bs + 32, total);
  put<uint32_t>(bs + 36, fatsz);
  put<uint32_t>(bs + 44, 2);
  put<uint16_t>(bs + 48, 1);
  put<uint16_t>(bs + 50, 6);
  bs[64] = 0x80;
  bs[66] = 0x29;
  put<uint32_t>(bs + 67, 0x43484d31);  // «CHM1»
  __builtin_memcpy(bs + 71, "NO NAME    ", 11);
  bool has_label = label != nullptr && label[0] != '\0';
  if (has_label) {
    __builtin_memset(bs + 71, ' ', 11);
    for (int i = 0; i < 11 && label[i] != '\0'; ++i) bs[71 + i] = static_cast<uint8_t>(upper(label[i]));
    uint8_t vol[kEntry] = {};
    __builtin_memcpy(vol, bs + 71, 11);
    vol[11] = kAttrVolumeId;
    put<uint16_t>(vol + 24, kDate1980);
    if (io.write(root_off, vol, kEntry).is_err()) return fail(FsError::IoError);
  }
  __builtin_memcpy(bs + 82, "FAT32   ", 8);
  bs[510] = 0x55, bs[511] = 0xaa;
  uint8_t fi[512] = {};
  put<uint32_t>(fi, 0x41615252);
  put<uint32_t>(fi + 484, 0x61417272);
  put<uint32_t>(fi + 488, clusters - 1);  // занят только корень
  put<uint32_t>(fi + 492, 3);
  put<uint32_t>(fi + 508, 0xaa550000);
  const uint32_t bases[] = {0, 6};  // основной и резервный загрузочный сектор
  for (uint32_t base : bases) {
    if (io.write(uint64_t{base} * kBps, bs, kBps).is_err() ||
        io.write(uint64_t{base + 1} * kBps, fi, kBps).is_err()) {
      return fail(FsError::IoError);
    }
  }
  // FAT[0] — медиа в младшем байте, FAT[1] — EOC, FAT[2] — корень (EOC).
  uint32_t head[3] = {0x0fffff00u | 0xf8u, kEoc, kEoc};
  for (uint32_t f = 0; f < kFats; ++f) {
    if (io.write((uint64_t{kReserved} + uint64_t{f} * fatsz) * kBps, head, sizeof head).is_err()) {
      return fail(FsError::IoError);
    }
  }
  return io.flush().is_err() ? fail(FsError::IoError) : ok();
}

}  // namespace lib::fs
