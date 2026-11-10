#include <lib/fs/ext2.hpp>

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

// Суперблок (ext2-doc табл. 3.3): смещения полей.
constexpr uint32_t kSInodesCount = 0;
constexpr uint32_t kSBlocksCount = 4;
constexpr uint32_t kSFreeBlocks = 12;
constexpr uint32_t kSFreeInodes = 16;
constexpr uint32_t kSFirstDataBlock = 20;
constexpr uint32_t kSLogBlockSize = 24;
constexpr uint32_t kSLogFragSize = 28;
constexpr uint32_t kSBlocksPerGroup = 32;
constexpr uint32_t kSFragsPerGroup = 36;
constexpr uint32_t kSInodesPerGroup = 40;
constexpr uint32_t kSMaxMntCount = 54;
constexpr uint32_t kSMagic = 56;
constexpr uint32_t kSState = 58;
constexpr uint32_t kSErrors = 60;
constexpr uint32_t kSRevLevel = 76;
constexpr uint32_t kSFirstIno = 84;
constexpr uint32_t kSInodeSize = 88;
constexpr uint32_t kSBlockGroupNr = 90;
constexpr uint32_t kSFeatureIncompat = 96;
constexpr uint32_t kSFeatureRoCompat = 100;
constexpr uint32_t kSUuid = 104;
constexpr uint32_t kSVolumeName = 120;
constexpr uint16_t kMagic = 0xef53;
constexpr uint32_t kIncompatFiletype = 0x0002;       // табл. 3.9
constexpr uint32_t kRoCompatSparseSuper = 0x0001;    // табл. 3.10
constexpr uint32_t kRoCompatLargeFile = 0x0002;

// Дескриптор группы (табл. 3.12), 32 байта.
constexpr uint32_t kGdSize = 32;
constexpr uint32_t kGBlockBitmap = 0;
constexpr uint32_t kGInodeBitmap = 4;
constexpr uint32_t kGInodeTable = 8;
constexpr uint32_t kGFreeBlocks = 12;
constexpr uint32_t kGFreeInodes = 14;
constexpr uint32_t kGUsedDirs = 16;

// i_mode (табл. 3.15), типы записей каталога (табл. 4.2).
constexpr uint16_t kModeFile = 0x8000;
constexpr uint16_t kModeDir = 0x4000;
constexpr uint16_t kModeTypeMask = 0xf000;
constexpr uint8_t kFtFile = 1;
constexpr uint8_t kFtDir = 2;
constexpr uint32_t kRootIno = 2;
constexpr uint32_t kDirect = 12;

uint32_t rec_len_for(uint32_t name_len) { return (8 + name_len + 3) & ~3u; }

}  // namespace

struct Ext2::Inode {
  uint16_t mode;
  uint16_t uid;
  uint32_t size;
  uint32_t atime, ctime, mtime, dtime;
  uint16_t gid;
  uint16_t links;
  uint32_t blocks;  // в секторах по 512 байт
  uint32_t flags;
  uint32_t osd1;
  uint32_t block[15];
  uint32_t generation;
  uint32_t file_acl;
  uint32_t dir_acl;  // ревизия 1, обычный файл: старшие 32 бита размера (LARGE_FILE)
  uint32_t faddr;
  uint8_t osd2[12];
};
static_assert(sizeof(Ext2::Inode) == 128);
static_assert(__builtin_offsetof(Ext2::Inode, links) == 26);
static_assert(__builtin_offsetof(Ext2::Inode, block) == 40);
static_assert(__builtin_offsetof(Ext2::Inode, dir_acl) == 108);

// --- монтирование ---------------------------------------------------------------------

Status Ext2::mount(Io* io) {
  io_ = io;
  if (io->read(1024, super_, sizeof super_).is_err()) return fail(FsError::IoError);
  if (get<uint16_t>(super_ + kSMagic) != kMagic) return fail(FsError::Corrupt);
  uint32_t log = get<uint32_t>(super_ + kSLogBlockSize);
  if (log > 2) return fail(FsError::Unsupported);  // блоки больше 4 КиБ
  block_size_ = 1024u << log;
  uint32_t rev = get<uint32_t>(super_ + kSRevLevel);
  if (rev > 1) return fail(FsError::Unsupported);
  inode_size_ = rev == 0 ? 128 : get<uint16_t>(super_ + kSInodeSize);
  first_ino_ = rev == 0 ? 11 : get<uint32_t>(super_ + kSFirstIno);
  if (inode_size_ < 128 || inode_size_ > block_size_ || (inode_size_ & (inode_size_ - 1)) != 0) {
    return fail(FsError::Corrupt);
  }
  uint32_t incompat = rev == 0 ? 0 : get<uint32_t>(super_ + kSFeatureIncompat);
  uint32_t ro_compat = rev == 0 ? 0 : get<uint32_t>(super_ + kSFeatureRoCompat);
  if ((incompat & ~kIncompatFiletype) != 0) return fail(FsError::Unsupported);
  filetype_ = (incompat & kIncompatFiletype) != 0;
  large_file_ = (ro_compat & kRoCompatLargeFile) != 0;
  read_only_ = (ro_compat & ~(kRoCompatSparseSuper | kRoCompatLargeFile)) != 0;
  blocks_count_ = get<uint32_t>(super_ + kSBlocksCount);
  inodes_count_ = get<uint32_t>(super_ + kSInodesCount);
  blocks_per_group_ = get<uint32_t>(super_ + kSBlocksPerGroup);
  inodes_per_group_ = get<uint32_t>(super_ + kSInodesPerGroup);
  first_data_block_ = get<uint32_t>(super_ + kSFirstDataBlock);
  if (blocks_per_group_ == 0 || inodes_per_group_ == 0 || blocks_count_ <= first_data_block_ ||
      blocks_per_group_ > block_size_ * 8 || inodes_per_group_ > block_size_ * 8) {
    return fail(FsError::Corrupt);
  }
  if (uint64_t{blocks_count_} * block_size_ > io->size()) return fail(FsError::Corrupt);
  groups_ = (blocks_count_ - first_data_block_ + blocks_per_group_ - 1) / blocks_per_group_;
  if (uint64_t{groups_} * inodes_per_group_ < inodes_count_) return fail(FsError::Corrupt);
  gdt_block_ = first_data_block_ + 1;  // табл. дескрипторов — за суперблоком
  Inode root_inode;
  if (read_inode(kRootIno, &root_inode).is_err()) return fail(FsError::IoError);
  if ((root_inode.mode & kModeTypeMask) != kModeDir) return fail(FsError::Corrupt);
  return ok();
}

Status Ext2::write_super() {
  return io_->write(1024, super_, sizeof super_).is_err() ? fail(FsError::IoError) : ok();
}

uint32_t Ext2::free_blocks() const { return get<uint32_t>(super_ + kSFreeBlocks); }
uint32_t Ext2::free_inodes() const { return get<uint32_t>(super_ + kSFreeInodes); }

Status Ext2::read_group(uint32_t g, uint8_t* desc) {
  return io_->read(block_offset(gdt_block_) + uint64_t{g} * kGdSize, desc, kGdSize).is_err()
             ? fail(FsError::IoError)
             : ok();
}

Status Ext2::write_group(uint32_t g, const uint8_t* desc) {
  return io_->write(block_offset(gdt_block_) + uint64_t{g} * kGdSize, desc, kGdSize).is_err()
             ? fail(FsError::IoError)
             : ok();
}

Status Ext2::read_inode(uint32_t ino, Inode* out) {
  if (ino == 0 || ino > inodes_count_) return fail(FsError::Corrupt);
  uint8_t gd[kGdSize];
  if (read_group((ino - 1) / inodes_per_group_, gd).is_err()) return fail(FsError::IoError);
  uint64_t off = block_offset(get<uint32_t>(gd + kGInodeTable)) +
                 uint64_t{(ino - 1) % inodes_per_group_} * inode_size_;
  return io_->read(off, out, sizeof *out).is_err() ? fail(FsError::IoError) : ok();
}

Status Ext2::write_inode(uint32_t ino, const Inode& in) {
  if (read_only_) return fail(FsError::ReadOnly);
  uint8_t gd[kGdSize];
  if (read_group((ino - 1) / inodes_per_group_, gd).is_err()) return fail(FsError::IoError);
  uint64_t off = block_offset(get<uint32_t>(gd + kGInodeTable)) +
                 uint64_t{(ino - 1) % inodes_per_group_} * inode_size_;
  return io_->write(off, &in, sizeof in).is_err() ? fail(FsError::IoError) : ok();
}

// --- выделение ----------------------------------------------------------------------------

Result<uint32_t> Ext2::alloc(bool inode, uint32_t near_group) {
  if (read_only_) return Result<uint32_t>::err(FsError::ReadOnly);
  for (uint32_t k = 0; k < groups_; ++k) {
    uint32_t g = (near_group + k) % groups_;
    uint8_t gd[kGdSize];
    if (read_group(g, gd).is_err()) return Result<uint32_t>::err(FsError::IoError);
    uint32_t free_off = inode ? kGFreeInodes : kGFreeBlocks;
    if (get<uint16_t>(gd + free_off) == 0) continue;
    uint32_t bitmap = get<uint32_t>(gd + (inode ? kGInodeBitmap : kGBlockBitmap));
    uint32_t per = inode ? inodes_per_group_ : blocks_per_group_;
    uint8_t chunk[256];
    for (uint32_t bit = 0; bit < per; bit += sizeof chunk * 8) {
      uint32_t bytes = (per - bit + 7) / 8 < sizeof chunk ? (per - bit + 7) / 8 : sizeof chunk;
      uint64_t off = block_offset(bitmap) + bit / 8;
      if (io_->read(off, chunk, bytes).is_err()) return Result<uint32_t>::err(FsError::IoError);
      for (uint32_t i = 0; i < bytes; ++i) {
        if (chunk[i] == 0xff) continue;
        for (uint32_t b = 0; b < 8; ++b) {
          uint32_t index = bit + i * 8 + b;
          if (index >= per || (chunk[i] & (1u << b)) != 0) continue;
          uint32_t number = inode ? g * inodes_per_group_ + index + 1
                                  : first_data_block_ + g * blocks_per_group_ + index;
          if (inode ? (number < first_ino_ || number > inodes_count_) : number >= blocks_count_) {
            continue;
          }
          chunk[i] = static_cast<uint8_t>(chunk[i] | (1u << b));
          if (io_->write(off + i, &chunk[i], 1).is_err()) return Result<uint32_t>::err(FsError::IoError);
          put<uint16_t>(gd + free_off, static_cast<uint16_t>(get<uint16_t>(gd + free_off) - 1));
          if (write_group(g, gd).is_err()) return Result<uint32_t>::err(FsError::IoError);
          uint32_t sfree = inode ? kSFreeInodes : kSFreeBlocks;
          put<uint32_t>(super_ + sfree, get<uint32_t>(super_ + sfree) - 1);
          if (!inode) {
            uint8_t z[512] = {};
            for (uint32_t o = 0; o < block_size_; o += sizeof z) {
              if (io_->write(block_offset(number) + o, z, sizeof z).is_err()) {
                return Result<uint32_t>::err(FsError::IoError);
              }
            }
          }
          return Result<uint32_t>::ok(number);
        }
      }
    }
  }
  return Result<uint32_t>::err(FsError::NoSpace);
}

Status Ext2::release(bool inode, uint32_t number) {
  uint32_t g, index;
  if (inode) {
    g = (number - 1) / inodes_per_group_;
    index = (number - 1) % inodes_per_group_;
  } else {
    if (number < first_data_block_ || number >= blocks_count_) return fail(FsError::Corrupt);
    g = (number - first_data_block_) / blocks_per_group_;
    index = (number - first_data_block_) % blocks_per_group_;
  }
  uint8_t gd[kGdSize];
  if (read_group(g, gd).is_err()) return fail(FsError::IoError);
  uint64_t off = block_offset(get<uint32_t>(gd + (inode ? kGInodeBitmap : kGBlockBitmap))) + index / 8;
  uint8_t byte;
  if (io_->read(off, &byte, 1).is_err()) return fail(FsError::IoError);
  if ((byte & (1u << (index % 8))) == 0) return fail(FsError::Corrupt);  // уже свободен
  byte = static_cast<uint8_t>(byte & ~(1u << (index % 8)));
  if (io_->write(off, &byte, 1).is_err()) return fail(FsError::IoError);
  uint32_t free_off = inode ? kGFreeInodes : kGFreeBlocks;
  put<uint16_t>(gd + free_off, static_cast<uint16_t>(get<uint16_t>(gd + free_off) + 1));
  if (write_group(g, gd).is_err()) return fail(FsError::IoError);
  uint32_t sfree = inode ? kSFreeInodes : kSFreeBlocks;
  put<uint32_t>(super_ + sfree, get<uint32_t>(super_ + sfree) + 1);
  return ok();
}

// --- блоки файла --------------------------------------------------------------------------

uint64_t Ext2::file_size(const Inode& in) const {
  uint64_t s = in.size;
  if (large_file_ && (in.mode & kModeTypeMask) == kModeFile) s |= uint64_t{in.dir_acl} << 32;
  return s;
}

void Ext2::set_file_size(Inode& in, uint64_t size) const {
  in.size = static_cast<uint32_t>(size);
  if ((in.mode & kModeTypeMask) == kModeFile) in.dir_acl = static_cast<uint32_t>(size >> 32);
}

Result<uint32_t> Ext2::bmap(Inode& in, uint64_t index, bool grow) {
  uint64_t per = block_size_ / 4;
  uint32_t* slot;
  uint32_t level;
  uint64_t rest;
  if (index < kDirect) {
    slot = &in.block[index];
    level = 0;
    rest = 0;
  } else if ((index -= kDirect) < per) {
    slot = &in.block[12], level = 1, rest = index;
  } else if ((index -= per) < per * per) {
    slot = &in.block[13], level = 2, rest = index;
  } else if ((index -= per * per) < per * per * per) {
    slot = &in.block[14], level = 3, rest = index;
  } else {
    return Result<uint32_t>::err(FsError::TooLarge);
  }
  uint32_t sectors_per_block = block_size_ / 512;
  uint32_t near = 0;  // группу подбирает alloc
  if (*slot == 0) {
    if (!grow) return Result<uint32_t>::ok(0);
    auto b = alloc(false, near);
    if (b.is_err()) return b;
    *slot = b.value();
    in.blocks += sectors_per_block;
  }
  uint32_t block = *slot;
  for (uint32_t l = level; l > 0; --l) {
    uint64_t span = 1;
    for (uint32_t k = 1; k < l; ++k) span *= per;
    uint64_t i = rest / span;
    rest %= span;
    uint64_t off = block_offset(block) + i * 4;
    uint32_t next;
    if (io_->read(off, &next, 4).is_err()) return Result<uint32_t>::err(FsError::IoError);
    if (next == 0) {
      if (!grow) return Result<uint32_t>::ok(0);
      auto b = alloc(false, near);
      if (b.is_err()) return b;
      next = b.value();
      in.blocks += sectors_per_block;
      if (io_->write(off, &next, 4).is_err()) return Result<uint32_t>::err(FsError::IoError);
    }
    if (next >= blocks_count_) return Result<uint32_t>::err(FsError::Corrupt);
    block = next;
  }
  return Result<uint32_t>::ok(block);
}

// Освобождает в поддереве *ptr (уровень level, первый логический блок
// first_index) всё, что начиная с keep; пустое поддерево обнуляет *ptr.
Status Ext2::truncate_tree(uint32_t* ptr, uint32_t level, uint64_t first_index, uint64_t keep,
                           Inode& in) {
  if (*ptr == 0) return ok();
  uint32_t sectors_per_block = block_size_ / 512;
  if (level == 0) {
    if (first_index >= keep) {
      if (release(false, *ptr).is_err()) return fail(FsError::Corrupt);
      in.blocks -= sectors_per_block;
      *ptr = 0;
    }
    return ok();
  }
  uint64_t per = block_size_ / 4;
  uint64_t span = 1;
  for (uint32_t k = 1; k < level; ++k) span *= per;
  bool empty = true;
  for (uint64_t i = 0; i < per; ++i) {
    uint64_t off = block_offset(*ptr) + i * 4;
    uint32_t child;
    if (io_->read(off, &child, 4).is_err()) return fail(FsError::IoError);
    if (child == 0) continue;
    uint64_t start = first_index + i * span;
    if (start + span > keep) {
      uint32_t c = child;
      Status s = truncate_tree(&c, level - 1, start, keep, in);
      if (s.is_err()) return s;
      if (c != child && io_->write(off, &c, 4).is_err()) return fail(FsError::IoError);
      child = c;
    }
    if (child != 0) empty = false;
  }
  if (empty) {
    if (release(false, *ptr).is_err()) return fail(FsError::Corrupt);
    in.blocks -= sectors_per_block;
    *ptr = 0;
  }
  return ok();
}

// --- каталоги -----------------------------------------------------------------------------

// Ищет name в каталоге dir: pos — смещение записи в каталоге, prev — смещение
// предыдущей записи того же блока (или ~0).
Status Ext2::find(uint32_t dir, StringView name, uint64_t* pos, uint32_t* ino, uint64_t* prev) {
  Inode in;
  if (read_inode(dir, &in).is_err()) return fail(FsError::IoError);
  if ((in.mode & kModeTypeMask) != kModeDir) return fail(FsError::NotDirectory);
  uint64_t size = in.size;
  for (uint64_t blk = 0; blk * block_size_ < size; ++blk) {
    auto pb = bmap(in, blk, false);
    if (pb.is_err()) return fail(pb.error());
    if (pb.value() == 0) continue;
    uint64_t last = ~uint64_t{0};
    for (uint32_t off = 0; off < block_size_;) {
      uint8_t h[8 + kMaxName];
      if (io_->read(block_offset(pb.value()) + off, h, 8).is_err()) return fail(FsError::IoError);
      uint32_t e_ino = get<uint32_t>(h);
      uint16_t rec = get<uint16_t>(h + 4);
      uint8_t nlen = h[6];
      if (rec < 8 || rec % 4 != 0 || off + rec > block_size_ || 8u + nlen > rec) {
        return fail(FsError::Corrupt);
      }
      if (e_ino != 0 && nlen == name.size()) {
        if (io_->read(block_offset(pb.value()) + off + 8, h + 8, nlen).is_err()) {
          return fail(FsError::IoError);
        }
        if (__builtin_memcmp(h + 8, name.data(), nlen) == 0) {
          *pos = blk * block_size_ + off;
          *ino = e_ino;
          *prev = last == ~uint64_t{0} ? last : blk * block_size_ + last;
          return ok();
        }
      }
      last = off;
      off += rec;
    }
  }
  return fail(FsError::NotFound);
}

Result<Node> Ext2::lookup(Node dir, StringView name) {
  uint64_t pos, prev;
  uint32_t ino;
  Status s = find(static_cast<uint32_t>(dir.id), name, &pos, &ino, &prev);
  if (s.is_err()) return Result<Node>::err(s.error());
  Inode in;
  if (read_inode(ino, &in).is_err()) return Result<Node>::err(FsError::IoError);
  NodeType t = (in.mode & kModeTypeMask) == kModeDir ? NodeType::Directory : NodeType::File;
  return Result<Node>::ok(Node{ino, t});
}

Status Ext2::read_dir(Node dir, uint64_t cookie, DirEntry* out, uint64_t* next) {
  Inode in;
  if (read_inode(static_cast<uint32_t>(dir.id), &in).is_err()) return fail(FsError::IoError);
  if ((in.mode & kModeTypeMask) != kModeDir) return fail(FsError::NotDirectory);
  for (uint64_t pos = cookie; pos < in.size;) {
    uint64_t blk = pos / block_size_;
    uint32_t off = static_cast<uint32_t>(pos % block_size_);
    auto pb = bmap(in, blk, false);
    if (pb.is_err()) return fail(pb.error());
    if (pb.value() == 0) {
      pos = (blk + 1) * block_size_;
      continue;
    }
    uint8_t h[8];
    if (io_->read(block_offset(pb.value()) + off, h, 8).is_err()) return fail(FsError::IoError);
    uint32_t e_ino = get<uint32_t>(h);
    uint16_t rec = get<uint16_t>(h + 4);
    uint8_t nlen = h[6];
    if (rec < 8 || rec % 4 != 0 || off + rec > block_size_ || 8u + nlen > rec) {
      return fail(FsError::Corrupt);
    }
    pos += rec;
    if (e_ino == 0 || nlen == 0) continue;
    char name[kMaxName + 1];
    if (io_->read(block_offset(pb.value()) + off + 8, name, nlen).is_err()) return fail(FsError::IoError);
    if ((nlen == 1 && name[0] == '.') || (nlen == 2 && name[0] == '.' && name[1] == '.')) continue;
    Inode child;
    if (read_inode(e_ino, &child).is_err()) return fail(FsError::IoError);
    out->node = {e_ino, (child.mode & kModeTypeMask) == kModeDir ? NodeType::Directory : NodeType::File};
    __builtin_memcpy(out->name, name, nlen);
    out->name[nlen] = '\0';
    out->name_len = nlen;
    *next = pos;
    return ok();
  }
  *next = 0;
  return ok();
}

Status Ext2::add_entry(uint32_t dir, StringView name, uint32_t ino, NodeType type) {
  Inode in;
  if (read_inode(dir, &in).is_err()) return fail(FsError::IoError);
  uint32_t need = rec_len_for(static_cast<uint32_t>(name.size()));
  uint8_t e[8 + kMaxName + 4] = {};
  put<uint32_t>(e, ino);
  e[6] = static_cast<uint8_t>(name.size());
  e[7] = filetype_ ? (type == NodeType::Directory ? kFtDir : kFtFile) : 0;
  __builtin_memcpy(e + 8, name.data(), name.size());

  for (uint64_t blk = 0; blk * block_size_ < in.size; ++blk) {
    auto pb = bmap(in, blk, false);
    if (pb.is_err()) return fail(pb.error());
    if (pb.value() == 0) continue;
    for (uint32_t off = 0; off < block_size_;) {
      uint8_t h[8];
      uint64_t at = block_offset(pb.value()) + off;
      if (io_->read(at, h, 8).is_err()) return fail(FsError::IoError);
      uint32_t e_ino = get<uint32_t>(h);
      uint16_t rec = get<uint16_t>(h + 4);
      if (rec < 8 || off + rec > block_size_) return fail(FsError::Corrupt);
      uint32_t used = e_ino == 0 ? 0 : rec_len_for(h[6]);
      if (rec - used >= need) {
        if (used == 0) {
          put<uint16_t>(e + 4, rec);
          if (io_->write(at, e, need).is_err()) return fail(FsError::IoError);
        } else {
          put<uint16_t>(h + 4, static_cast<uint16_t>(used));
          put<uint16_t>(e + 4, static_cast<uint16_t>(rec - used));
          // Populate the still-hidden tail before publishing its new record.
          // An I/O error must not expose an uninitialized directory entry.
          if (io_->write(at + used, e, need).is_err() || io_->write(at, h, 8).is_err()) {
            return fail(FsError::IoError);
          }
        }
        return ok();
      }
      off += rec;
    }
  }
  // Места нет: новый блок каталога с одной записью на весь блок.
  uint64_t blk = in.size / block_size_;
  auto pb = bmap(in, blk, true);
  if (pb.is_err()) return fail(pb.error());
  put<uint16_t>(e + 4, static_cast<uint16_t>(block_size_));
  if (io_->write(block_offset(pb.value()), e, need).is_err()) return fail(FsError::IoError);
  in.size += block_size_;
  return write_inode(dir, in);
}

Result<Node> Ext2::create(Node dir_node, StringView name, NodeType type) {
  if (read_only_) return Result<Node>::err(FsError::ReadOnly);
  if (!valid_name(name)) return Result<Node>::err(FsError::InvalidName);
  auto dir = static_cast<uint32_t>(dir_node.id);
  uint64_t pos, prev;
  uint32_t existing;
  Status s = find(dir, name, &pos, &existing, &prev);
  if (s.is_ok()) return Result<Node>::err(FsError::AlreadyExists);
  if (s.error() != FsError::NotFound) return Result<Node>::err(s.error());

  auto ino_r = alloc(true, (dir - 1) / inodes_per_group_);
  if (ino_r.is_err()) return Result<Node>::err(ino_r.error());
  uint32_t ino = ino_r.value();
  Inode in = {};
  in.mode = type == NodeType::Directory ? static_cast<uint16_t>(kModeDir | 0755)
                                        : static_cast<uint16_t>(kModeFile | 0644);
  in.links = type == NodeType::Directory ? 2 : 1;
  if (type == NodeType::Directory) {
    auto b = bmap(in, 0, true);
    if (b.is_err()) {
      (void)release(true, ino);
      return Result<Node>::err(b.error());
    }
    uint8_t d[24] = {};
    put<uint32_t>(d, ino);
    put<uint16_t>(d + 4, 12);
    d[6] = 1;
    d[7] = filetype_ ? kFtDir : 0;
    d[8] = '.';
    put<uint32_t>(d + 12, dir);
    put<uint16_t>(d + 16, static_cast<uint16_t>(block_size_ - 12));
    d[18] = 2;
    d[19] = filetype_ ? kFtDir : 0;
    d[20] = '.', d[21] = '.';
    if (io_->write(block_offset(b.value()), d, sizeof d).is_err()) return Result<Node>::err(FsError::IoError);
    in.size = block_size_;
  }
  if (write_inode(ino, in).is_err()) return Result<Node>::err(FsError::IoError);
  Status a = add_entry(dir, name, ino, type);
  if (a.is_err()) return Result<Node>::err(a.error());
  if (type == NodeType::Directory) {
    Inode parent;
    if (read_inode(dir, &parent).is_err()) return Result<Node>::err(FsError::IoError);
    ++parent.links;  // «..» нового каталога
    if (write_inode(dir, parent).is_err()) return Result<Node>::err(FsError::IoError);
    uint32_t g = (ino - 1) / inodes_per_group_;
    uint8_t gd[kGdSize];
    if (read_group(g, gd).is_err()) return Result<Node>::err(FsError::IoError);
    put<uint16_t>(gd + kGUsedDirs, static_cast<uint16_t>(get<uint16_t>(gd + kGUsedDirs) + 1));
    if (write_group(g, gd).is_err()) return Result<Node>::err(FsError::IoError);
  }
  return Result<Node>::ok(Node{ino, type});
}

Status Ext2::unlink(Node dir_node, StringView name) {
  if (read_only_) return fail(FsError::ReadOnly);
  auto dir = static_cast<uint32_t>(dir_node.id);
  uint64_t pos, prev;
  uint32_t ino;
  Status s = find(dir, name, &pos, &ino, &prev);
  if (s.is_err()) return s;
  Inode in;
  if (read_inode(ino, &in).is_err()) return fail(FsError::IoError);
  bool is_dir = (in.mode & kModeTypeMask) == kModeDir;
  if (is_dir) {
    DirEntry e;
    uint64_t next;
    Status r = read_dir(Node{ino, NodeType::Directory}, 0, &e, &next);
    if (r.is_err()) return r;
    if (next != 0) return fail(FsError::NotEmpty);
  }
  Status erased=erase_entry(dir,name);
  if(erased.is_err()) return erased;

  in.links = static_cast<uint16_t>(in.links - (is_dir ? 2 : 1));
  if (is_dir) {
    Inode parent;
    if (read_inode(dir, &parent).is_err()) return fail(FsError::IoError);
    --parent.links;
    if (write_inode(dir, parent).is_err()) return fail(FsError::IoError);
    uint32_t g = (ino - 1) / inodes_per_group_;
    uint8_t gd[kGdSize];
    if (read_group(g, gd).is_err()) return fail(FsError::IoError);
    put<uint16_t>(gd + kGUsedDirs, static_cast<uint16_t>(get<uint16_t>(gd + kGUsedDirs) - 1));
    if (write_group(g, gd).is_err()) return fail(FsError::IoError);
  }
  return retire_inode(ino,in);
}

Status Ext2::erase_entry(uint32_t dir,StringView name) {
  uint64_t pos=0,prev=0;
  uint32_t ino=0;
  auto found=find(dir,name,&pos,&ino,&prev);
  if(found.is_err()) return found;
  // Remove only the name, retaining inode contents and link accounting.
  Inode d;
  if (read_inode(dir, &d).is_err()) return fail(FsError::IoError);
  auto pb = bmap(d, pos / block_size_, false);
  if (pb.is_err() || pb.value() == 0) return fail(FsError::Corrupt);
  uint64_t base = block_offset(pb.value());
  uint8_t h[8];
  if (io_->read(base + pos % block_size_, h, 8).is_err()) return fail(FsError::IoError);
  if (prev == ~uint64_t{0}) {
    put<uint32_t>(h, 0);
    if (io_->write(base + pos % block_size_, h, 4).is_err()) return fail(FsError::IoError);
  } else {
    uint8_t ph[8];
    if (io_->read(base + prev % block_size_, ph, 8).is_err()) return fail(FsError::IoError);
    put<uint16_t>(ph + 4, static_cast<uint16_t>(get<uint16_t>(ph + 4) + get<uint16_t>(h + 4)));
    if (io_->write(base + prev % block_size_, ph, 8).is_err()) return fail(FsError::IoError);
  }

  return ok();
}

Status Ext2::replace_entry(uint32_t dir,StringView name,uint32_t inode) {
  uint64_t pos=0,prev=0;
  uint32_t old_inode=0;
  auto found=find(dir,name,&pos,&old_inode,&prev);
  if(found.is_err()) return found;
  Inode directory;
  if(read_inode(dir,&directory).is_err()) return fail(FsError::IoError);
  auto block=bmap(directory,pos/block_size_,false);
  if(block.is_err() || !block.value()) return fail(FsError::Corrupt);
  uint8_t value[4]; put<uint32_t>(value,inode);
  return io_->write(block_offset(block.value())+pos%block_size_,value,sizeof value);
}

Status Ext2::retire_inode(uint32_t ino,Inode& in) {
  if (in.links == 0 || static_cast<int16_t>(in.links) < 0) {
    in.links = 0;
    uint64_t keep = 0;
    for (uint32_t i = 0; i < 15; ++i) {
      uint64_t per = block_size_ / 4;
      uint64_t first = i < kDirect ? i : i == 12 ? kDirect : i == 13 ? kDirect + per : kDirect + per + per * per;
      Status t = truncate_tree(&in.block[i], i < kDirect ? 0 : i - 11, first, keep, in);
      if (t.is_err()) return t;
    }
    in.size = 0;
    in.dir_acl = 0;
    // Время удаления. Часов реального времени пока нет: фиксированная метка
    // 2026-01-01. Значение меньше числа inode e2fsck принял бы за ссылку списка
    // «сирот» (поле dtime так используется для них).
    in.dtime = inodes_count_ > 1767225600u ? inodes_count_ : 1767225600u;
    if (write_inode(ino, in).is_err()) return fail(FsError::IoError);
    return release(true, ino);
  }
  return write_inode(ino, in);
}

Status Ext2::rename(Node source_dir,StringView source,Node target_dir,StringView target) {
  if(read_only_) return fail(FsError::ReadOnly);
  if(!valid_name(source) || !valid_name(target)) return fail(FsError::InvalidName);
  auto from=lookup(source_dir,source);
  if(from.is_err()) return fail(from.error());
  auto to=lookup(target_dir,target);
  if(to.is_err() && to.error()!=FsError::NotFound) return fail(to.error());
  if(to.is_ok() && to.value().id==from.value().id) return ok();
  Inode original;
  auto inode=static_cast<uint32_t>(from.value().id);
  if(read_inode(inode,&original).is_err()) return fail(FsError::IoError);
  if((original.mode&kModeTypeMask)!=kModeFile) return fail(FsError::Unsupported);
  if(!original.links) return fail(FsError::Corrupt);
  Inode replaced{};
  uint32_t old_inode=0;
  if(to.is_ok()) {
    old_inode=static_cast<uint32_t>(to.value().id);
    if(read_inode(old_inode,&replaced).is_err()) return fail(FsError::IoError);
    if((replaced.mode&kModeTypeMask)==kModeDir) return fail(FsError::IsDirectory);
    if((replaced.mode&kModeTypeMask)!=kModeFile) return fail(FsError::Unsupported);
    if(!replaced.links) return fail(FsError::Corrupt);
  }
  uint32_t source_parent=static_cast<uint32_t>(source_dir.id);
  uint32_t target_parent=static_cast<uint32_t>(target_dir.id);
  auto published=old_inode ? replace_entry(target_parent,target,inode) : add_entry(target_parent,target,inode,NodeType::File);
  if(published.is_err()) return published;
  auto removed=erase_entry(source_parent,source);
  if(removed.is_err()) {
    // A failed write may still have removed the source. Keep the destination
    // unless the old name is definitely still reachable with the same inode.
    auto remaining=lookup(source_dir,source);
    if(remaining.is_ok() && remaining.value().id==inode) {
      if(old_inode) (void)replace_entry(target_parent,target,old_inode);
      else (void)erase_entry(target_parent,target);
    }
    return removed;
  }
  if(old_inode) {
    --replaced.links;
    return retire_inode(old_inode,replaced);
  }
  return ok();
}

// --- файлы --------------------------------------------------------------------------------

Result<Stat> Ext2::stat(Node node) {
  Inode in;
  if (read_inode(static_cast<uint32_t>(node.id), &in).is_err()) return Result<Stat>::err(FsError::IoError);
  bool dir = (in.mode & kModeTypeMask) == kModeDir;
  return Result<Stat>::ok(Stat{dir ? 0 : file_size(in), dir ? NodeType::Directory : NodeType::File,
                               uint64_t{in.mtime} * 1'000'000'000ull});
}

Result<size_t> Ext2::read(Node file, uint64_t offset, void* buf, size_t len) {
  Inode in;
  if (read_inode(static_cast<uint32_t>(file.id), &in).is_err()) return Result<size_t>::err(FsError::IoError);
  if ((in.mode & kModeTypeMask) == kModeDir) return Result<size_t>::err(FsError::IsDirectory);
  uint64_t size = file_size(in);
  if (offset >= size) return Result<size_t>::ok(0);
  if (len > size - offset) len = static_cast<size_t>(size - offset);
  auto* out = static_cast<uint8_t*>(buf);
  for (size_t done = 0; done < len;) {
    uint64_t pos = offset + done;
    uint32_t in_block = static_cast<uint32_t>(pos % block_size_);
    size_t chunk = block_size_ - in_block < len - done ? block_size_ - in_block : len - done;
    uint64_t first = pos / block_size_;
    auto pb = bmap(in, first, false);
    if (pb.is_err()) return Result<size_t>::err(pb.error());
    if (pb.value() == 0) {
      __builtin_memset(out + done, 0, chunk);  // дыра
    } else {
      // Соседние блоки файла обычно лежат подряд: читаем весь непрерывный
      // участок одним запросом к диску. Без этого запуск большого ELF
      // (content_shell, 200 МиБ) распадается на десятки тысяч чтений по 4 КиБ.
      size_t total = chunk;
      uint32_t blocks = 1;
      while (done + total < len && (offset + done + total) % block_size_ == 0) {
        auto nb = bmap(in, first + blocks, false);
        if (nb.is_err()) return Result<size_t>::err(nb.error());
        if (nb.value() != pb.value() + blocks) break;
        size_t step = block_size_ < len - done - total ? block_size_ : len - done - total;
        total += step;
        ++blocks;
      }
      if (io_->read(block_offset(pb.value()) + in_block, out + done, total).is_err()) {
        return Result<size_t>::err(FsError::IoError);
      }
      done += total;
      continue;
    }
    done += chunk;
  }
  return Result<size_t>::ok(len);
}

Result<size_t> Ext2::write(Node file, uint64_t offset, const void* buf, size_t len) {
  if (read_only_) return Result<size_t>::err(FsError::ReadOnly);
  auto ino = static_cast<uint32_t>(file.id);
  Inode in;
  if (read_inode(ino, &in).is_err()) return Result<size_t>::err(FsError::IoError);
  if ((in.mode & kModeTypeMask) == kModeDir) return Result<size_t>::err(FsError::IsDirectory);
  if (len == 0) return Result<size_t>::ok(0);
  if (!large_file_ && offset + len > 0x7fffffffull) return Result<size_t>::err(FsError::TooLarge);
  const auto* src = static_cast<const uint8_t*>(buf);
  size_t done = 0;
  Status err = ok();
  while (done < len) {
    uint64_t pos = offset + done;
    uint32_t in_block = static_cast<uint32_t>(pos % block_size_);
    size_t chunk = block_size_ - in_block < len - done ? block_size_ - in_block : len - done;
    auto pb = bmap(in, pos / block_size_, true);
    if (pb.is_err()) {
      err = fail(pb.error());
      break;
    }
    if (io_->write(block_offset(pb.value()) + in_block, src + done, chunk).is_err()) {
      err = fail(FsError::IoError);
      break;
    }
    done += chunk;
  }
  if (offset + done > file_size(in)) set_file_size(in, offset + done);
  if (write_inode(ino, in).is_err()) return Result<size_t>::err(FsError::IoError);
  if (err.is_err()) return Result<size_t>::err(err.error());
  return Result<size_t>::ok(len);
}

Status Ext2::resize(Node file, uint64_t size) {
  if (read_only_) return fail(FsError::ReadOnly);
  auto ino = static_cast<uint32_t>(file.id);
  Inode in;
  if (read_inode(ino, &in).is_err()) return fail(FsError::IoError);
  if ((in.mode & kModeTypeMask) == kModeDir) return fail(FsError::IsDirectory);
  if (!large_file_ && size > 0x7fffffffull) return fail(FsError::TooLarge);
  uint64_t old = file_size(in);
  if (size < old) {
    // Хвост последнего оставшегося блока — нулями (потом он может снова стать частью файла).
    if (size % block_size_ != 0) {
      auto pb = bmap(in, size / block_size_, false);
      if (pb.is_err()) return fail(pb.error());
      if (pb.value() != 0) {
        uint8_t z[512] = {};
        for (uint64_t p = size % block_size_; p < block_size_;) {
          size_t chunk = block_size_ - p < sizeof z ? static_cast<size_t>(block_size_ - p) : sizeof z;
          if (io_->write(block_offset(pb.value()) + p, z, chunk).is_err()) return fail(FsError::IoError);
          p += chunk;
        }
      }
    }
    uint64_t keep = (size + block_size_ - 1) / block_size_;
    uint64_t per = block_size_ / 4;
    for (uint32_t i = 0; i < 15; ++i) {
      uint64_t first = i < kDirect ? i : i == 12 ? kDirect : i == 13 ? kDirect + per : kDirect + per + per * per;
      Status t = truncate_tree(&in.block[i], i < kDirect ? 0 : i - 11, first, keep, in);
      if (t.is_err()) return t;
    }
  }
  set_file_size(in, size);  // рост — дыра, читается нулями
  return write_inode(ino, in);
}

Status Ext2::sync() {
  if (!read_only_ && write_super().is_err()) return fail(FsError::IoError);
  return io_->flush().is_err() ? fail(FsError::IoError) : ok();
}

// --- форматирование -------------------------------------------------------------------------

Status Ext2::format(Io& io, uint32_t block_size, const char* label) {
  if (block_size != 1024 && block_size != 2048 && block_size != 4096) return fail(FsError::Unsupported);
  uint64_t blocks64 = io.size() / block_size;
  if (blocks64 > 0xffffffffull) return fail(FsError::TooLarge);
  auto blocks = static_cast<uint32_t>(blocks64);
  uint32_t first_data = block_size == 1024 ? 1 : 0;
  uint32_t bpg = block_size * 8;
  uint32_t groups = (blocks - first_data + bpg - 1) / bpg;
  // Inode на каждые 4 блока, целыми блоками таблицы и кратно 8.
  uint32_t per_block = block_size / 128;
  uint32_t ipg = bpg / 4;
  if (ipg > block_size * 8) ipg = block_size * 8;
  ipg = (ipg + per_block - 1) / per_block * per_block;
  uint32_t itable_blocks = ipg / per_block;
  uint32_t gdt_blocks = (groups * kGdSize + block_size - 1) / block_size;
  uint32_t overhead = 1 + gdt_blocks + 2 + itable_blocks;
  uint32_t last_group_blocks = blocks - first_data - (groups - 1) * bpg;
  if (last_group_blocks < overhead + 8) {
    if (groups == 1) return fail(FsError::NoSpace);
    --groups;  // хвост меньше служебной части группы — не используется
    blocks = first_data + groups * bpg;
    last_group_blocks = bpg;
  }

  // Обнулить служебные области всех групп и вычислить счётчики.
  uint8_t z[4096] = {};
  auto zero = [&](uint64_t block, uint32_t count) -> Status {
    for (uint32_t i = 0; i < count; ++i) {
      if (io.write((block + i) * block_size, z, block_size).is_err()) return fail(FsError::IoError);
    }
    return ok();
  };
  uint32_t total_free_blocks = 0;
  uint8_t gdt[4096 * 4] = {};
  if (gdt_blocks > 4) return fail(FsError::TooLarge);
  // Корень (inode 2) и lost+found (inode 11) — по блоку в группе 0.
  uint32_t g0_data = first_data + overhead;
  uint32_t root_block = g0_data, lf_block = g0_data + 1;
  for (uint32_t g = 0; g < groups; ++g) {
    uint32_t start = first_data + g * bpg;
    uint32_t count = g + 1 == groups ? blocks - start : bpg;
    uint32_t bb = start + 1 + gdt_blocks, ib = bb + 1, it = ib + 1;
    if (zero(bb, 2 + itable_blocks).is_err()) return fail(FsError::IoError);
    // Битовая карта блоков: служебные + за концом группы заняты.
    uint8_t bitmap[4096] = {};
    uint32_t used = overhead + (g == 0 ? 2 : 0);
    for (uint32_t b = 0; b < bpg; ++b) {
      if (b < used || b >= count) bitmap[b / 8] = static_cast<uint8_t>(bitmap[b / 8] | (1u << (b % 8)));
    }
    if (io.write(uint64_t{bb} * block_size, bitmap, block_size).is_err()) return fail(FsError::IoError);
    uint8_t ibitmap[4096] = {};
    uint32_t iused = g == 0 ? 11 : 0;  // 1..10 зарезервированы, 11 — lost+found
    for (uint32_t i = 0; i < block_size * 8; ++i) {
      if (i < iused || i >= ipg) ibitmap[i / 8] = static_cast<uint8_t>(ibitmap[i / 8] | (1u << (i % 8)));
    }
    if (io.write(uint64_t{ib} * block_size, ibitmap, block_size).is_err()) return fail(FsError::IoError);
    uint8_t* gd = gdt + g * kGdSize;
    put<uint32_t>(gd + kGBlockBitmap, bb);
    put<uint32_t>(gd + kGInodeBitmap, ib);
    put<uint32_t>(gd + kGInodeTable, it);
    put<uint16_t>(gd + kGFreeBlocks, static_cast<uint16_t>(count - used));
    put<uint16_t>(gd + kGFreeInodes, static_cast<uint16_t>(ipg - iused));
    put<uint16_t>(gd + kGUsedDirs, static_cast<uint16_t>(g == 0 ? 2 : 0));
    total_free_blocks += count - used;
  }

  uint8_t sb[1024] = {};
  put<uint32_t>(sb + kSInodesCount, groups * ipg);
  put<uint32_t>(sb + kSBlocksCount, blocks);
  put<uint32_t>(sb + kSFreeBlocks, total_free_blocks);
  put<uint32_t>(sb + kSFreeInodes, groups * ipg - 11);
  put<uint32_t>(sb + kSFirstDataBlock, first_data);
  uint32_t log = block_size == 1024 ? 0 : block_size == 2048 ? 1 : 2;
  put<uint32_t>(sb + kSLogBlockSize, log);
  put<uint32_t>(sb + kSLogFragSize, log);
  put<uint32_t>(sb + kSBlocksPerGroup, bpg);
  put<uint32_t>(sb + kSFragsPerGroup, bpg);
  put<uint32_t>(sb + kSInodesPerGroup, ipg);
  put<uint16_t>(sb + kSMaxMntCount, 0xffff);
  put<uint16_t>(sb + kSMagic, kMagic);
  put<uint16_t>(sb + kSState, 1);   // чисто размонтирована
  put<uint16_t>(sb + kSErrors, 1);  // продолжать
  put<uint32_t>(sb + kSRevLevel, 1);
  put<uint32_t>(sb + kSFirstIno, 11);
  put<uint16_t>(sb + kSInodeSize, 128);
  put<uint32_t>(sb + kSFeatureIncompat, kIncompatFiletype);
  for (uint32_t i = 0; i < 16; ++i) sb[kSUuid + i] = static_cast<uint8_t>(0x43 + i * 7);
  for (uint32_t i = 0; i < 16 && label != nullptr && label[i] != '\0'; ++i) sb[kSVolumeName + i] = static_cast<uint8_t>(label[i]);
  for (uint32_t g = 0; g < groups; ++g) {
    uint32_t start = first_data + g * bpg;
    put<uint16_t>(sb + kSBlockGroupNr, static_cast<uint16_t>(g));
    uint64_t sb_off = g == 0 ? 1024 : uint64_t{start} * block_size;
    if (io.write(sb_off, sb, sizeof sb).is_err()) return fail(FsError::IoError);
    if (io.write(uint64_t{start + 1} * block_size, gdt, gdt_blocks * block_size).is_err()) {
      return fail(FsError::IoError);
    }
  }

  // Корень и lost+found.
  uint32_t it0 = get<uint32_t>(gdt + kGInodeTable);
  auto make_dir = [&](uint32_t ino, uint32_t parent, uint32_t block, uint16_t links) -> Status {
    Inode in = {};
    in.mode = static_cast<uint16_t>(kModeDir | 0755);
    in.links = links;
    in.size = block_size;
    in.blocks = block_size / 512;
    in.block[0] = block;
    if (io.write(uint64_t{it0} * block_size + uint64_t{ino - 1} * 128, &in, sizeof in).is_err()) {
      return fail(FsError::IoError);
    }
    uint8_t d[4096] = {};
    put<uint32_t>(d, ino);
    put<uint16_t>(d + 4, 12);
    d[6] = 1, d[7] = kFtDir, d[8] = '.';
    put<uint32_t>(d + 12, parent);
    if (ino == kRootIno) {
      put<uint16_t>(d + 16, 20);
      d[18] = 2, d[19] = kFtDir, d[20] = '.', d[21] = '.';
      put<uint32_t>(d + 32, 11);
      put<uint16_t>(d + 36, static_cast<uint16_t>(block_size - 32));
      d[38] = 10, d[39] = kFtDir;
      __builtin_memcpy(d + 40, "lost+found", 10);
    } else {
      put<uint16_t>(d + 16, static_cast<uint16_t>(block_size - 12));
      d[18] = 2, d[19] = kFtDir, d[20] = '.', d[21] = '.';
    }
    return io.write(uint64_t{block} * block_size, d, block_size).is_err() ? fail(FsError::IoError) : ok();
  };
  if (make_dir(kRootIno, kRootIno, root_block, 3).is_err() ||
      make_dir(11, kRootIno, lf_block, 2).is_err()) {
    return fail(FsError::IoError);
  }
  return io.flush().is_err() ? fail(FsError::IoError) : ok();
}

}  // namespace lib::fs
