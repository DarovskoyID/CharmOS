#pragma once
// Блочный слой ядра (docs/abi/block.md): диски, которые обслуживают драйверы
// класса block в юзерспейсе. Запрос — до 6 страниц по 4 КиБ, подряд на диске;
// страницы на время запроса отображаются в домен IOMMU драйвера.
//
// Пока запрос на диск один в каждый момент (кольцо на 64 места используется
// на одно): правило 9 — очередь глубже только после замера.

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::ipc {
struct Cap;
}

namespace kernel::block {

using abi::syscall::Status;

inline constexpr uint32_t kMaxDisks = 8;
inline constexpr uint32_t kMaxPages = 6;  // abi::block::kMaxSegments

struct DiskInfo {
  uint32_t block_size;
  uint64_t block_count;
  bool read_only;
  bool online;  // драйвер работает
  bool failed;  // драйвер исчерпал перезапуски: все запросы — IoError
};

uint32_t disk_count();
bool disk_info(uint32_t disk, DiskInfo* out);
// Ждёт, пока диск появится и драйвер будет готов. false — срок истёк.
bool wait_online(uint32_t disk, uint64_t deadline_ns);

// Чтение или запись count страниц (физ. адреса pages) начиная с блока lba.
// Каждая страница — 4096 байт диска. Возвращается после завершения (при
// падении драйвера запрос повторяется в новом запуске).
Status transfer(uint32_t disk, bool write, uint64_t lba, const uint64_t* pages, uint32_t count);
// Сброс кэша записи устройства (если оно его не умеет — сразу Ok).
Status flush(uint32_t disk);

// --- для менеджера драйверов ------------------------------------------------------------
// Обслуживает один запуск блочного драйвера функции pci_index: рукопожатие
// по стартовому каналу, затем ждёт, пока драйвер не уйдёт (канал закрыт).
void serve(uint32_t pci_index, ipc::Cap* kernel_end, const char* name);
// Драйвер исчерпал перезапуски.
void driver_failed(uint32_t pci_index);

}  // namespace kernel::block
