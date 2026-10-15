#pragma once
// Разбор и проверка пакета .cpk (docs/abi/package.md) — для службы пакетов
// и хост-инструмента tools/sign. Данные пакета не копируются: View
// указывает в переданный буфер.

#include <abi/package.hpp>
#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

namespace lib::package {

enum class Error : uint8_t {
  Ok,
  Truncated,      // выход за размер файла
  BadMagic,
  BadVersion,
  BadLayout,      // смещения, порядок областей, резервные поля
  BadPath,        // пустой, «..», начальный «/», не завершён нулём
  BadManifest,
  NoExec,         // exec не найден или не исполняемый
  Unsigned,
  BadSignature,
  UntrustedKey,
  BadHash,
};
const char* error_name(Error e);

struct Manifest {
  char id[65];
  char name[65];
  char version[33];
  char exec[abi::package::kMaxPath];
  uint32_t permissions;  // abi::package::kPerm*
};

struct View {
  const abi::package::Header* header;
  const abi::package::FileEntry* files;  // header->file_count
  lib::core::StringView manifest_text;
  const uint8_t* signature;  // 64 байта или nullptr
  const uint8_t* signer;     // 32 байта открытого ключа или nullptr
  Manifest manifest;
  const uint8_t* base;
  uint64_t size;
  bool meta_only;  // разобран package.sig: данных файлов нет
};

// Полный пакет: проверяет раскладку, пути, манифест (подпись — отдельно).
lib::core::Result<View, Error> open(const uint8_t* data, uint64_t size);
// package.sig (§7 уточнений): подписанная часть [0, data_offset) и блок подписи.
lib::core::Result<View, Error> open_meta(const uint8_t* data, uint64_t size);

// Ok — подпись есть, верна и сделана ключом trusted (или любым, если trusted == nullptr).
Error check_signature(const View& v, const uint8_t* trusted);
// SHA-512 содержимого совпадает с записью таблицы.
bool check_file(const abi::package::FileEntry& f, const uint8_t* content, uint64_t size);
// Хеши всех файлов полного пакета (Ok или BadHash).
Error check_files(const View& v);

// Запись таблицы по пути или nullptr.
const abi::package::FileEntry* find(const View& v, lib::core::StringView path);

// Манифест (§3): строгое подмножество TOML.
lib::core::Result<Manifest, Error> parse_manifest(lib::core::StringView text);

}  // namespace lib::package
