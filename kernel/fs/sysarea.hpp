#pragma once
// Закрытая область (docs/abi/sysarea-crypt.md §4): ключ из TPM, первичное
// шифрование на месте, вид для ФС. Без TPM — область заперта.

#include <lib/crypto/aes.hpp>
#include <lib/fs/fs.hpp>
#include <lib/sysarea/sysarea.hpp>

namespace kernel::fs {

// part — весь раздел. true — out готов для монтирования ФС.
bool sysarea_unlock(lib::fs::Io* part, lib::crypto::Xts256* key, lib::sysarea::XtsIo* out);

}  // namespace kernel::fs
