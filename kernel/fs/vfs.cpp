#include "vfs.hpp"

#include <abi/fs.hpp>
#include <lib/fs/ext2.hpp>
#include <lib/fs/fat32.hpp>
#include <lib/fs/gpt.hpp>

#include "../block/block.hpp"
#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
#include "../mm/heap.hpp"
#include "../platform/platform.hpp"
#include "../proc/uaccess.hpp"
#include "../sync/spinlock.hpp"
#include "../sync/wait.hpp"
#include "cache.hpp"

namespace kernel::fs {

namespace {

using lib::core::StringView;
using lib::fs::FsError;
using lib::fs::Node;
using lib::fs::NodeType;
namespace rights = abi::syscall::rights;

constinit sync::Mutex g_vfs{"vfs"};
constinit DiskIo g_io;
constinit lib::fs::Ext2 g_ext2;
constinit lib::fs::Fat32 g_fat;
constinit lib::fs::FileSystem* g_fs = nullptr;

// Сколько хендлов открыто на каждый узел: удалять открытое нельзя (fs.md §5).
constexpr uint32_t kMaxOpen = 512;
struct OpenCount {
  uint64_t node;
  uint32_t count;
};
constinit sync::Spinlock g_open_lock{sync::LockRank::Block, "vfs.open"};
constinit OpenCount g_open[kMaxOpen] = {};

bool open_ref(uint64_t node) {
  sync::SpinGuard guard(g_open_lock);
  OpenCount* free_slot = nullptr;
  for (OpenCount& o : g_open) {
    if (o.count != 0 && o.node == node) {
      ++o.count;
      return true;
    }
    if (o.count == 0 && free_slot == nullptr) free_slot = &o;
  }
  if (free_slot == nullptr) return false;
  *free_slot = {node, 1};
  return true;
}

void open_unref(uint64_t node) {
  sync::SpinGuard guard(g_open_lock);
  for (OpenCount& o : g_open) {
    if (o.count != 0 && o.node == node) {
      --o.count;
      return;
    }
  }
}

bool is_open(uint64_t node) {
  sync::SpinGuard guard(g_open_lock);
  for (const OpenCount& o : g_open) {
    if (o.count != 0 && o.node == node) return true;
  }
  return false;
}

Status map(FsError e) {
  switch (e) {
    case FsError::NotFound: return Status::NotFound;
    case FsError::AlreadyExists: return Status::AlreadyExists;
    case FsError::NoSpace: return Status::NoSpace;
    case FsError::NotDirectory:
    case FsError::IsDirectory: return Status::WrongType;
    case FsError::NotEmpty: return Status::Busy;
    case FsError::InvalidName: return Status::InvalidArgs;
    case FsError::ReadOnly: return Status::AccessDenied;
    case FsError::Unsupported: return Status::NotSupported;
    case FsError::TooLarge: return Status::OutOfRange;
    case FsError::IoError:
    case FsError::Corrupt: return Status::IoError;
  }
  return Status::IoError;
}

// Путь из памяти процесса в кучу ядра (fs.md §4: до 4096 байт).
Status copy_path(uint64_t user, uint64_t len, char** out) {
  if (len > abi::fs::kMaxPath) return Status::InvalidArgs;
  auto mem = mm::kmalloc(len + 1);
  if (mem.is_err()) return Status::NoMemory;
  auto* p = static_cast<char*>(mem.value());
  if (len != 0 && !proc::copy_from_user(p, user, len)) {
    mm::kfree(p);
    return Status::BadAddress;
  }
  p[len] = '\0';
  *out = p;
  return Status::Ok;
}

// Разбирает путь (без начального '/', компоненты непустые, без «.» и «..»):
// идёт от dir до родителя последней компоненты. *last — она (пустая — сам dir).
Status walk_parent(Node dir, StringView path, Node* parent, StringView* last) {
  *parent = dir;
  *last = StringView();
  if (path.empty()) return Status::Ok;
  size_t start = 0;
  for (;;) {
    size_t end = start;
    while (end < path.size() && path[end] != '/') ++end;
    StringView comp = path.substr(start, end - start);
    if (!lib::fs::valid_name(comp)) return Status::InvalidArgs;
    if (end == path.size()) {
      *last = comp;
      return Status::Ok;
    }
    auto next = g_fs->lookup(*parent, comp);
    if (next.is_err()) return map(next.error());
    if (next.value().type != NodeType::Directory) return Status::WrongType;
    *parent = next.value();
    start = end + 1;
  }
}

Status node_from(uint32_t h, uint32_t need, bool want_dir, Node* node, uint32_t* r) {
  if (g_fs == nullptr) return Status::NotSupported;
  uint64_t id;
  bool dir;
  Status s = ipc::get_fs_node(h, need, &id, &dir, r);
  if (s != Status::Ok) return s;
  if (dir != want_dir) return Status::WrongType;
  *node = {id, dir ? NodeType::Directory : NodeType::File};
  return Status::Ok;
}

void node_closed(uint64_t node) { open_unref(node); }

}  // namespace

bool mounted() { return g_fs != nullptr; }
const char* root_type() { return g_fs != nullptr ? g_fs->type_name() : "none"; }

bool mount_root(StringView spec) {
  // «disk<N>p<M>»
  uint32_t disk = 0, part = 0;
  size_t i = 4;
  if (!spec.starts_with("disk") || spec.size() < 7) {
    kprint("vfs: bad root= '{}'\n", spec);
    return false;
  }
  while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') disk = disk * 10 + static_cast<uint32_t>(spec[i++] - '0');
  if (i >= spec.size() || spec[i] != 'p') {
    kprint("vfs: bad root= '{}'\n", spec);
    return false;
  }
  ++i;
  while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') part = part * 10 + static_cast<uint32_t>(spec[i++] - '0');
  if (i != spec.size() || part == 0) {
    kprint("vfs: bad root= '{}'\n", spec);
    return false;
  }
  if (!block::wait_online(disk, platform::now_ns() + 20'000'000'000ull)) {
    kprint("vfs: disk{} not available\n", disk);
    return false;
  }
  block::DiskInfo info;
  (void)block::disk_info(disk, &info);
  sync::MutexGuard guard(g_vfs);
  DiskIo whole;
  whole.init(disk, 0, info.block_count * info.block_size);
  lib::fs::GptInfo gpt;
  if (auto r = lib::fs::read_gpt(whole, &gpt); r.is_err()) {
    kprint("vfs: disk{}: no valid GPT ({})\n", disk, lib::fs::error_name(r.error()));
    return false;
  }
  if (part > gpt.count) {
    kprint("vfs: disk{} has no partition {}\n", disk, part);
    return false;
  }
  const lib::fs::GptPartition& p = gpt.parts[part - 1];
  uint64_t bs = info.block_size;
  g_io.init(disk, p.first_lba * 512, (p.last_lba - p.first_lba + 1) * 512);
  (void)bs;
  ipc::set_fs_node_closed_hook(&node_closed);
  if (g_ext2.mount(&g_io).is_ok()) {
    g_fs = &g_ext2;
  } else if (g_fat.mount(&g_io).is_ok()) {
    g_fs = &g_fat;
  } else {
    kprint("vfs: disk{}p{}: neither ext2 nor FAT32\n", disk, part);
    return false;
  }
  kprint("vfs: root is disk{}p{} ({}{})\n", disk, part, g_fs->type_name(),
         g_fs->read_only() ? ", read-only" : "");
  return true;
}

Status root_cap(ipc::Cap** out) {
  if (g_fs == nullptr) return Status::NotSupported;
  Node root = g_fs->root();
  if (!open_ref(root.id)) return Status::NoMemory;
  Status s = ipc::create_fs_node(root.id, true,
                                 rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite,
                                 out);
  if (s != Status::Ok) open_unref(root.id);
  return s;
}

Status sync_all() {
  if (g_fs == nullptr) return Status::Ok;
  sync::MutexGuard guard(g_vfs);
  return g_fs->sync().is_ok() ? Status::Ok : Status::IoError;
}

Status sys_fs_open(uint32_t dir_h, uint64_t user_path, uint64_t len, uint64_t flags, uint64_t* out) {
  if ((flags & ~abi::fs::kOpenAll) != 0) return Status::InvalidArgs;
  bool want_write = (flags & abi::fs::kOpenWrite) != 0;
  bool want_dir = (flags & abi::fs::kOpenDirectory) != 0;
  bool create = (flags & abi::fs::kOpenCreate) != 0;
  if ((flags & abi::fs::kOpenTruncate) != 0 && !want_write) return Status::InvalidArgs;
  if ((flags & abi::fs::kOpenExclusive) != 0 && !create) return Status::InvalidArgs;
  if (want_dir && (flags & abi::fs::kOpenTruncate) != 0) return Status::InvalidArgs;
  Node dir;
  uint32_t dir_rights;
  Status s = node_from(dir_h, rights::kRead, true, &dir, &dir_rights);
  if (s != Status::Ok) return s;
  if ((want_write || create) && (dir_rights & rights::kWrite) == 0) return Status::AccessDenied;
  char* path;
  s = copy_path(user_path, len, &path);
  if (s != Status::Ok) return s;

  Node node;
  {
    sync::MutexGuard guard(g_vfs);
    Node parent;
    StringView last;
    s = walk_parent(dir, StringView(path, len), &parent, &last);
    if (s == Status::Ok) {
      if (last.empty()) {
        node = parent;
        if (create && (flags & abi::fs::kOpenExclusive) != 0) s = Status::AlreadyExists;
      } else {
        auto found = g_fs->lookup(parent, last);
        if (found.is_ok()) {
          if (create && (flags & abi::fs::kOpenExclusive) != 0) {
            s = Status::AlreadyExists;
          } else {
            node = found.value();
          }
        } else if (found.error() == FsError::NotFound && create) {
          auto made = g_fs->create(parent, last, want_dir ? NodeType::Directory : NodeType::File);
          if (made.is_err()) {
            s = map(made.error());
          } else {
            node = made.value();
          }
        } else {
          s = map(found.error());
        }
      }
    }
    if (s == Status::Ok && (node.type == NodeType::Directory) != want_dir) s = Status::WrongType;
    if (s == Status::Ok && (flags & abi::fs::kOpenTruncate) != 0) {
      if (auto t = g_fs->resize(node, 0); t.is_err()) s = map(t.error());
    }
  }
  mm::kfree(path);
  if (s != Status::Ok) return s;
  uint32_t r = dir_rights & (rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite);
  if (!want_dir && !want_write) r &= ~rights::kWrite;
  if (!open_ref(node.id)) return Status::NoMemory;
  return ipc::install_fs_node(node.id, want_dir, r, out);  // при ошибке хук снимет счётчик
}

namespace {

Status transfer(uint32_t h, uint64_t user, uint64_t len, uint64_t offset, bool write, uint64_t* out) {
  if (len > abi::fs::kMaxTransfer) return Status::InvalidArgs;
  Node file;
  uint32_t r;
  Status s = node_from(h, write ? rights::kWrite : rights::kRead, false, &file, &r);
  if (s != Status::Ok) return s;
  auto mem = mm::kmalloc(4096);
  if (mem.is_err()) return Status::NoMemory;
  auto* buf = static_cast<uint8_t*>(mem.value());
  uint64_t done = 0;
  s = Status::Ok;
  while (done < len) {
    size_t chunk = len - done < 4096 ? static_cast<size_t>(len - done) : 4096;
    if (write) {
      if (!proc::copy_from_user(buf, user + done, chunk)) {
        s = Status::BadAddress;
        break;
      }
      sync::MutexGuard guard(g_vfs);
      auto w = g_fs->write(file, offset + done, buf, chunk);
      if (w.is_err()) {
        s = map(w.error());
        break;
      }
    } else {
      size_t got;
      {
        sync::MutexGuard guard(g_vfs);
        auto rd = g_fs->read(file, offset + done, buf, chunk);
        if (rd.is_err()) {
          s = map(rd.error());
          break;
        }
        got = rd.value();
      }
      if (got != 0 && !proc::copy_to_user(user + done, buf, got)) {
        s = Status::BadAddress;
        break;
      }
      done += got;
      if (got < chunk) break;  // конец файла
      continue;
    }
    done += chunk;
  }
  mm::kfree(buf);
  *out = done;
  return done != 0 && s != Status::BadAddress ? Status::Ok : s;
}

}  // namespace

Status sys_file_read(uint32_t h, uint64_t buf, uint64_t len, uint64_t offset, uint64_t* out) {
  return transfer(h, buf, len, offset, false, out);
}

Status sys_file_write(uint32_t h, uint64_t buf, uint64_t len, uint64_t offset, uint64_t* out) {
  return transfer(h, buf, len, offset, true, out);
}

Status sys_file_sync(uint32_t h) {
  Node file;
  uint32_t r;
  Status s = node_from(h, rights::kRead, false, &file, &r);
  if (s != Status::Ok) return s;
  return sync_all();
}

Status read_file(uint32_t h, uint64_t max, void** data, uint64_t* size) {
  Node file;
  uint32_t r;
  Status s = node_from(h, rights::kRead, false, &file, &r);
  if (s != Status::Ok) return s;
  uint64_t len;
  {
    sync::MutexGuard guard(g_vfs);
    auto st = g_fs->stat(file);
    if (st.is_err()) return map(st.error());
    len = st.value().size;
  }
  if (len == 0 || len > max) return Status::InvalidArgs;
  auto mem = mm::kmalloc(static_cast<size_t>(len));
  if (mem.is_err()) return Status::NoMemory;
  for (uint64_t done = 0; done < len;) {
    sync::MutexGuard guard(g_vfs);
    size_t chunk = len - done < 65536 ? static_cast<size_t>(len - done) : 65536;
    auto rd = g_fs->read(file, done, static_cast<uint8_t*>(mem.value()) + done, chunk);
    if (rd.is_err() || rd.value() == 0) {
      mm::kfree(mem.value());
      return rd.is_err() ? map(rd.error()) : Status::IoError;
    }
    done += rd.value();
  }
  *data = mem.value();
  *size = len;
  return Status::Ok;
}

Status sys_fs_stat(uint32_t h, uint64_t user_out) {
  if (g_fs == nullptr) return Status::NotSupported;
  uint64_t id;
  bool dir;
  uint32_t r;
  Status s = ipc::get_fs_node(h, 0, &id, &dir, &r);
  if (s != Status::Ok) return s;
  abi::fs::Stat st = {};
  {
    sync::MutexGuard guard(g_vfs);
    auto res = g_fs->stat(Node{id, dir ? NodeType::Directory : NodeType::File});
    if (res.is_err()) return map(res.error());
    st.size = res.value().size;
    st.type = res.value().type == NodeType::Directory ? abi::fs::kTypeDirectory : abi::fs::kTypeFile;
    st.mtime_ns = res.value().mtime_ns;
  }
  return proc::copy_to_user(user_out, &st, sizeof st) ? Status::Ok : Status::BadAddress;
}

Status sys_dir_read(uint32_t h, uint64_t user_buf, uint64_t cap, uint64_t cookie, uint64_t* out) {
  Node dir;
  uint32_t r;
  Status s = node_from(h, rights::kRead, true, &dir, &r);
  if (s != Status::Ok) return s;
  if (cap > 65536) cap = 65536;
  auto mem = mm::kmalloc(cap + 1);
  auto ent_mem = mm::kmalloc(sizeof(lib::fs::DirEntry));
  if (mem.is_err() || ent_mem.is_err()) {
    if (mem.is_ok()) mm::kfree(mem.value());
    if (ent_mem.is_ok()) mm::kfree(ent_mem.value());
    return Status::NoMemory;
  }
  auto* buf = static_cast<uint8_t*>(mem.value());
  auto* e = static_cast<lib::fs::DirEntry*>(ent_mem.value());
  uint64_t used = 0, next = cookie;
  {
    sync::MutexGuard guard(g_vfs);
    for (;;) {
      uint64_t after;
      auto rd = g_fs->read_dir(dir, next, e, &after);
      if (rd.is_err()) {
        s = map(rd.error());
        break;
      }
      if (after == 0) break;  // конец каталога
      uint64_t rec = (sizeof(abi::fs::DirEntryHeader) + e->name_len + 7) & ~uint64_t{7};
      if (used + rec > cap) {
        if (used == 0) s = Status::BufferTooSmall;
        break;
      }
      abi::fs::DirEntryHeader hdr = {static_cast<uint16_t>(rec),
                                     static_cast<uint8_t>(e->node.type == NodeType::Directory
                                                              ? abi::fs::kTypeDirectory
                                                              : abi::fs::kTypeFile),
                                     e->name_len};
      __builtin_memset(buf + used, 0, rec);
      __builtin_memcpy(buf + used, &hdr, sizeof hdr);
      __builtin_memcpy(buf + used + sizeof hdr, e->name, e->name_len);
      used += rec;
      next = after;
    }
  }
  // Уточнение fs.md §4.2: за последней записью — record_len = 0, если есть место.
  if (s == Status::Ok && used + 2 <= cap) {
    buf[used] = 0;
    buf[used + 1] = 0;
    used += 2;
  }
  if (s == Status::Ok && used != 0 && !proc::copy_to_user(user_buf, buf, used)) s = Status::BadAddress;
  mm::kfree(buf);
  mm::kfree(e);
  *out = next;
  return s;
}

Status sys_fs_unlink(uint32_t dir_h, uint64_t user_path, uint64_t len) {
  Node dir;
  uint32_t r;
  Status s = node_from(dir_h, rights::kWrite, true, &dir, &r);
  if (s != Status::Ok) return s;
  char* path;
  s = copy_path(user_path, len, &path);
  if (s != Status::Ok) return s;
  {
    sync::MutexGuard guard(g_vfs);
    Node parent;
    StringView last;
    s = walk_parent(dir, StringView(path, len), &parent, &last);
    if (s == Status::Ok && last.empty()) s = Status::InvalidArgs;
    if (s == Status::Ok) {
      auto found = g_fs->lookup(parent, last);
      if (found.is_err()) {
        s = map(found.error());
      } else if (is_open(found.value().id)) {
        s = Status::Busy;  // на узел есть хендлы
      } else if (auto u = g_fs->unlink(parent, last); u.is_err()) {
        s = map(u.error());
      }
    }
  }
  mm::kfree(path);
  return s;
}

Status sys_file_resize(uint32_t h, uint64_t size) {
  Node file;
  uint32_t r;
  Status s = node_from(h, rights::kWrite, false, &file, &r);
  if (s != Status::Ok) return s;
  sync::MutexGuard guard(g_vfs);
  auto rs = g_fs->resize(file, size);
  return rs.is_ok() ? Status::Ok : map(rs.error());
}

}  // namespace kernel::fs
