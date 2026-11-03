#pragma once
// Общие интерфейсы файловых систем. Код ФС не знает, откуда байты: ядро
// подставляет page cache поверх блочного слоя, хост-тесты и tools/mkdisk —
// память или файл. Динамической памяти ФС не выделяет: буферы — внутри
// объектов.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

namespace lib::fs {

enum class FsError : uint8_t {
  IoError,
  NotFound,
  AlreadyExists,
  NoSpace,
  NotDirectory,
  IsDirectory,
  NotEmpty,
  InvalidName,
  ReadOnly,
  Corrupt,      // структуры на диске противоречат спецификации
  Unsupported,  // формат понятен, но возможность не поддержана
  TooLarge,
};

const char* error_name(FsError e);

template <typename T>
using Result = lib::core::Result<T, FsError>;
using Status = lib::core::Status<FsError>;

inline Status ok() { return lib::core::ok_status<FsError>(); }
inline Status fail(FsError e) { return Status::err(e); }

// Байтовый доступ к разделу (смещения — от начала раздела).
class Io {
 public:
  virtual Status read(uint64_t offset, void* buf, size_t len) = 0;
  virtual Status write(uint64_t offset, const void* buf, size_t len) = 0;
  // Всё записанное — на носителе.
  virtual Status flush() = 0;
  virtual uint64_t size() const = 0;

 protected:
  ~Io() = default;
};

enum class NodeType : uint8_t { File = 1, Directory = 2 };

// Узел ФС: id — постоянный для этой ФС (FAT: место записи каталога; ext2:
// номер inode). Узлы не держат ресурсов: копировать можно свободно.
struct Node {
  uint64_t id;
  NodeType type;
};

struct Stat {
  uint64_t size;
  NodeType type;
  uint64_t mtime_ns;  // 0 — не хранится
};

inline constexpr size_t kMaxName = 255;

struct DirEntry {
  Node node;
  uint8_t name_len;
  char name[kMaxName + 1];
};

class FileSystem {
 public:
  virtual const char* type_name() const = 0;
  virtual bool read_only() const = 0;
  virtual Node root() = 0;

  virtual Result<Node> lookup(Node dir, lib::core::StringView name) = 0;
  // Создаёт пустой файл или каталог; AlreadyExists, если имя занято.
  virtual Result<Node> create(Node dir, lib::core::StringView name, NodeType type) = 0;
  // Удаляет файл или пустой каталог.
  virtual Status unlink(Node dir, lib::core::StringView name) = 0;
  // Atomic to externally serialized filesystem observers; unsupported backends
  // fail explicitly. No cross-filesystem copying is performed by rename.
  virtual Status rename(Node,lib::core::StringView,Node,lib::core::StringView) {
    return fail(FsError::Unsupported);
  }

  virtual Result<Stat> stat(Node node) = 0;
  // Читает до len байт; меньше — конец файла.
  virtual Result<size_t> read(Node file, uint64_t offset, void* buf, size_t len) = 0;
  // Пишет всё или ошибка; за концом файла — дыра из нулей.
  virtual Result<size_t> write(Node file, uint64_t offset, const void* buf, size_t len) = 0;
  virtual Status resize(Node file, uint64_t size) = 0;

  // Следующая запись каталога начиная с cookie (0 — с начала). Ok и
  // *next == 0 — записей больше нет. «.» и «..» не выдаются.
  virtual Status read_dir(Node dir, uint64_t cookie, DirEntry* out, uint64_t* next) = 0;

  // Метаданные ФС (счётчики свободного места и т. п.) — в Io; затем Io.flush.
  virtual Status sync() = 0;

 protected:
  ~FileSystem() = default;
};

// Имя компоненты пути: непустое, без '/' и '\0', не «.» и не «..», до 255 байт.
bool valid_name(lib::core::StringView name);

}  // namespace lib::fs
