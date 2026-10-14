#pragma once
// ext2, ревизии 0 и 1 (ИСТОЧНИК ВТОРИЧНЫЙ: docs/specs/ext2-doc.html, «The
// Second Extended File System», Dave Poirier; официальной спецификации нет).
// Суперблок, дескрипторы групп, inode с прямыми и косвенными блоками
// (до тройных), связные каталоги. Индексированные каталоги (HTree) читаются
// как связные (так они и устроены на диске) и при записи не поддерживаются.
//
// Узел — номер inode. Несовместимые возможности, кроме FILETYPE, — отказ;
// неизвестные ro_compat — монтирование только для чтения.

#include <lib/fs/fs.hpp>

namespace lib::fs {

class Ext2 final : public FileSystem {
 public:
  Status mount(Io* io);
  // Форматирует весь Io: ревизия 1, inode по 128 байт, FILETYPE, копии
  // суперблока во всех группах (без sparse_super), корень и lost+found.
  static Status format(Io& io, uint32_t block_size, const char* label);

  const char* type_name() const override { return "ext2"; }
  bool read_only() const override { return read_only_; }
  Node root() override { return {2, NodeType::Directory}; }
  Result<Node> lookup(Node dir, lib::core::StringView name) override;
  Result<Node> create(Node dir, lib::core::StringView name, NodeType type) override;
  Status unlink(Node dir, lib::core::StringView name) override;
  Result<Stat> stat(Node node) override;
  Result<size_t> read(Node file, uint64_t offset, void* buf, size_t len) override;
  Result<size_t> write(Node file, uint64_t offset, const void* buf, size_t len) override;
  Status resize(Node file, uint64_t size) override;
  Status read_dir(Node dir, uint64_t cookie, DirEntry* out, uint64_t* next) override;
  Status sync() override;

  uint32_t free_blocks() const;
  uint32_t free_inodes() const;

  struct Inode;  // первые 128 байт inode

 private:
  uint64_t block_offset(uint32_t block) const { return uint64_t{block} * block_size_; }
  Status read_inode(uint32_t ino, Inode* out);
  Status write_inode(uint32_t ino, const Inode& in);
  Status read_group(uint32_t g, uint8_t* desc);
  Status write_group(uint32_t g, const uint8_t* desc);
  Result<uint32_t> alloc(bool inode, uint32_t near_group);
  Status release(bool inode, uint32_t number);
  // Физический блок логического блока index (0 — дыра); при grow — выделяет
  // (обнулённые), обновляя i_blocks.
  Result<uint32_t> bmap(Inode& in, uint64_t index, bool grow);
  Status truncate_tree(uint32_t* ptr, uint32_t level, uint64_t first_index, uint64_t keep,
                       Inode& in);
  uint64_t file_size(const Inode& in) const;
  void set_file_size(Inode& in, uint64_t size) const;
  Status find(uint32_t dir, lib::core::StringView name, uint64_t* pos, uint32_t* ino,
              uint64_t* prev);
  Status add_entry(uint32_t dir, lib::core::StringView name, uint32_t ino, NodeType type);
  Status write_super();

  Io* io_ = nullptr;
  bool read_only_ = false;
  bool filetype_ = false;
  bool large_file_ = false;
  uint32_t block_size_ = 0;
  uint32_t inode_size_ = 0;
  uint32_t first_ino_ = 0;
  uint32_t groups_ = 0;
  uint32_t blocks_per_group_ = 0;
  uint32_t inodes_per_group_ = 0;
  uint32_t first_data_block_ = 0;
  uint32_t blocks_count_ = 0;
  uint32_t inodes_count_ = 0;
  uint32_t gdt_block_ = 0;
  uint8_t super_[1024] = {};
};

}  // namespace lib::fs
