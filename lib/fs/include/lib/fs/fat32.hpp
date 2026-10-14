#pragma once
// FAT32 (docs/specs/fatgen103.doc, Microsoft FAT32 File System Specification
// 1.03): загрузочный сектор и BPB, FSInfo, FAT, короткие и длинные записи
// каталога. FAT12/16 не поддерживаются (тип определяется числом кластеров).
//
// Узел — смещение его короткой записи каталога от начала раздела; корень — 0.
// Имена сравниваются без учёта регистра ASCII (как в FAT).

#include <lib/fs/fs.hpp>

namespace lib::fs {

class Fat32 final : public FileSystem {
 public:
  // Проверяет BPB и тип по числу кластеров. Io должен жить, пока жив объект.
  Status mount(Io* io);
  // Форматирует весь Io в FAT32 (2 копии FAT, 32 резервных сектора, FSInfo в
  // секторе 1, резервный загрузочный сектор в 6, корень в кластере 2).
  static Status format(Io& io, uint32_t sectors_per_cluster, const char* label);

  const char* type_name() const override { return "fat32"; }
  bool read_only() const override { return false; }
  Node root() override { return {0, NodeType::Directory}; }
  Result<Node> lookup(Node dir, lib::core::StringView name) override;
  Result<Node> create(Node dir, lib::core::StringView name, NodeType type) override;
  Status unlink(Node dir, lib::core::StringView name) override;
  Result<Stat> stat(Node node) override;
  Result<size_t> read(Node file, uint64_t offset, void* buf, size_t len) override;
  Result<size_t> write(Node file, uint64_t offset, const void* buf, size_t len) override;
  Status resize(Node file, uint64_t size) override;
  Status read_dir(Node dir, uint64_t cookie, DirEntry* out, uint64_t* next) override;
  Status sync() override;

  uint32_t cluster_count() const { return clusters_; }
  Result<uint32_t> free_clusters();

 public:
  struct Raw;   // короткая запись каталога (32 байта)
  struct Found;

 private:

  uint64_t cluster_offset(uint32_t c) const;
  Result<uint32_t> fat_get(uint32_t c);
  Status fat_set(uint32_t c, uint32_t v);
  Result<uint32_t> alloc_cluster(bool zero);
  Status free_chain(uint32_t first);
  // Кластер с номером index в цепочке first; при grow достраивает цепочку
  // (*first меняется, если цепочки не было).
  Result<uint32_t> chain_at(uint32_t* first, uint64_t index, bool grow, bool zero);

  Result<uint32_t> dir_cluster(Node dir);
  Status read_raw(uint64_t entry_offset, Raw* out);
  Status write_raw(uint64_t entry_offset, const Raw& r);
  Status set_first_cluster(Node node, uint32_t cluster);
  // Смещение записи number каталога, начинающегося с кластера first; при grow
  // каталог удлиняется (новые кластеры обнулены).
  Result<uint64_t> dir_entry_offset(uint32_t first, uint64_t number, bool grow);
  Status find(uint32_t dir_first, lib::core::StringView name, Found* out);
  Status scan(uint32_t dir_first, uint64_t from, Found* out, bool want_any);

  Io* io_ = nullptr;
  uint32_t bytes_per_sector_ = 0;
  uint32_t cluster_bytes_ = 0;
  uint64_t fat_offset_ = 0;
  uint64_t fat_bytes_ = 0;
  uint32_t num_fats_ = 0;
  uint64_t data_offset_ = 0;
  uint32_t clusters_ = 0;
  uint32_t root_cluster_ = 0;
  uint32_t fsinfo_sector_ = 0;
  uint32_t next_free_ = 2;
  bool fsinfo_dirty_ = false;
};

}  // namespace lib::fs
