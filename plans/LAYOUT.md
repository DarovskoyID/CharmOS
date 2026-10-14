# Раскладка репозитория

Каталоги создаются по мере наступления фазы (ROADMAP.md), а не заранее.
Пустые заготовки «на будущее» не заводить.

```
CharmOS/
├── CLAUDE.md              точка входа для агента
├── CMakeLists.txt         корневой: подключает подпроекты по целям
├── cmake/
│   ├── toolchain-uefi.cmake     clang, x86_64-unknown-windows, PE32+   (M0)
│   ├── toolchain-kernel.cmake   clang, x86_64-elf, freestanding        (M0)
│   ├── toolchain-user.cmake     clang, x86_64-charmos                  (M3)
│   └── flags.cmake              общие флаги, -Werror, предупреждения
├── plans/                 планы и журнал решений (не код)
├── docs/
│   ├── abi/               стабильные интерфейсы: boot.md, syscalls.md, ipc.md, driver.md, class-*.md
│   └── specs/             PDF спецификаций (UEFI, Intel SDM, ACPI, PCIe, VIRTIO, AHCI, NVMe…)
├── include/abi/           C++-заголовки ABI — единственный код, общий для boot/kernel/user/drivers
│                           (структуры со static_assert, номера syscall, коды ошибок, версии)
├── lib/                   общий freestanding-код без зависимости от ядра:
│   ├── core/              Result, Span, intrusive-списки, битмапы, форматирование, memcpy/memset
│   └── <name>/            парсеры (ELF, TOML, FAT, ext2-структуры) — тестируются на хосте
├── boot/uefi/             загрузчик (M1)
├── arch/x86_64/           ВСЁ платформенно-зависимое: GDT/IDT, paging, APIC, SMP, syscall entry, asm
├── kernel/
│   ├── core/              точка входа, паника, лог, init-последовательность
│   ├── mm/                PMM, VMM, heap
│   ├── sched/             потоки, планировщик
│   ├── cap/               таблица хендлов, права
│   ├── ipc/               каналы, порты, shared memory
│   ├── acpi/              таблицы (M2), AML-интерпретатор (M9)
│   ├── pci/               ECAM, MSI/MSI-X, IOMMU (M5)
│   ├── io/                менеджер устройств, DMA API, порты прерываний (M5)
│   ├── block/             блочный слой, page cache (M6)
│   ├── fs/                VFS, fat32/, ext2/ (M6)
│   └── net/               TCP/IP стек, сокеты (M7)
├── drivers/<имя>/         юзерспейс-драйвер: исходники + manifest.toml (M5+)
├── user/
│   ├── rt/                crt0, обёртки syscall (M3)
│   ├── libc/              своя libc (M8)
│   ├── init/              init и менеджер служб (M8)
│   └── bin/<имя>/         шелл, утилиты, httpd (M8)
├── compat/linux/          слой совместимости (M11, только после решения о лицензии)
├── tests/
│   ├── host/              юнит-тесты lib/ и чистой логики ядра, собираются обычным clang
│   └── qemu/<фаза>/       сценарии: QEMU-конфигурация + ожидаемый serial-вывод
└── tools/                 run.sh, test.sh, mkimage.sh, gdbinit, qemu-конфигурации
```

## Правила зависимостей

- `arch/x86_64/` — единственное место для x86-специфики (inline asm, MSR, порты
  ввода-вывода, форматы таблиц страниц). Остальной код ходит через интерфейсы
  в `arch/include/arch/*.hpp`.
- `include/abi/` не зависит ни от чего, кроме `<cstdint>` и `<cstddef>`.
- `lib/` не зависит от `kernel/` и `arch/`, поэтому собирается и для хост-тестов.
- `kernel/` не подключает заголовки `boot/` — только `include/abi/boot.hpp`.
- `drivers/` видят только `include/abi/` и `user/rt` (+ `user/libc` после M8).

## Именование

- Файлы: `snake_case.cpp` / `.hpp`; asm — `.S`.
- Пространства имён по каталогу: `kernel::mm`, `arch::x86_64`, `abi`, `lib::core`.
- Типы — `PascalCase`, функции и переменные — `snake_case`, константы — `kSnakeCase`.
