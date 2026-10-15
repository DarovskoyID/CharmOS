#pragma once
// VFS (docs/abi/fs.md): одна смонтированная корневая ФС, файлы и каталоги —
// хендлы на её узлы. Все операции ФС под одной блокировкой (правило 9).

#include <abi/syscall.hpp>
#include <lib/core/string_view.hpp>
#include <stdint.h>

namespace kernel::ipc {
struct Cap;
}

namespace kernel::fs {

using abi::syscall::Status;

// Монтирует корень по строке «disk<N>p<M>» (fs.md §3): ждёт диск, читает
// GPT, определяет ФС (ext2 или FAT32). Пишет результат в журнал.
bool mount_root(lib::core::StringView spec);
bool mounted();
const char* root_type();

// Отсоединённая капабилити на корень (права Duplicate | Transfer | Read | Write).
Status root_cap(ipc::Cap** out);
// Закрытая системная область (docs/abi/pkg.md §1): раздел GPT с типом
// CharmOS System Area на корневом диске, вторая ФС. Хендл — только init.
bool system_mounted();
// Для ktest: файл name в корне закрытой области (до cap байт).
Status system_read(lib::core::StringView name, void* buf, uint64_t cap, uint64_t* got);
Status system_cap(ipc::Cap** out);

// Сбросить всё на диск (метаданные ФС, кэш, кэш устройства).
Status sync_all();

// Файл по хендлу (право Read) целиком в куче ядра (освободить — mm::kfree).
// Пустой или больше max — InvalidArgs.
Status read_file(uint32_t file, uint64_t max, void** data, uint64_t* size);

// --- системные вызовы 23–30 ---------------------------------------------------------------
Status sys_fs_open(uint32_t dir, uint64_t path, uint64_t len, uint64_t flags, uint64_t* out);
Status sys_file_read(uint32_t file, uint64_t buf, uint64_t len, uint64_t offset, uint64_t* out);
Status sys_file_write(uint32_t file, uint64_t buf, uint64_t len, uint64_t offset, uint64_t* out);
Status sys_file_sync(uint32_t file);
Status sys_fs_stat(uint32_t h, uint64_t out);
Status sys_dir_read(uint32_t dir, uint64_t buf, uint64_t cap, uint64_t cookie, uint64_t* out);
Status sys_fs_unlink(uint32_t dir, uint64_t path, uint64_t len);
Status sys_file_resize(uint32_t file, uint64_t size);

}  // namespace kernel::fs
