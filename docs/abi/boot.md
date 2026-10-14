# ABI: передача управления загрузчик → ядро

**Статус: ЗАФИКСИРОВАН, версия 1.0 (2026-10-14).** Дальше
меняется только новой версией (ROADMAP, правило 2).

Код: `include/abi/boot.hpp` (единственный общий заголовок загрузчика и ядра).

## 1. Принципы

- Ядро не знает ничего о UEFI и о загрузчике, кроме этой структуры. После входа
  в ядро Boot Services уже закрыты (`ExitBootServices` выполнен).
- Вся информация передаётся одной структурой `BootInfo` и тем, на что она
  ссылается. Никаких «договорённостей по умолчанию» вне документа.
- Все указатели в `BootInfo` — **физические адреса** (`uint64_t`), кроме
  явно помеченных. Ядро само решает, как их отобразить.
- Все структуры: фиксированный размер, явные поля-заполнители, `static_assert`
  на размер и смещения. Только `uint*_t`, без `bool`, `enum class` с явным
  базовым типом, без указателей-типов.
- Расширяемость: в каждой структуре `size` (байт); ядро читает только поля,
  попадающие в `size`. Новые поля добавляются только в конец → минорная версия.
  Изменение смысла или удаление поля → новая мажорная версия.

## 2. Состояние машины при входе в ядро

| Что | Значение |
|---|---|
| Режим | long mode, CPL 0, только BSP; AP не запущены (стоят в состоянии после сброса/INIT от прошивки) |
| Прерывания | запрещены (`IF=0`); NMI возможны |
| Таблицы страниц | загрузчика, см. §4; CR3 указывает на них |
| GDT | загрузчика, плоская: `CS` — 64-битный код, `DS/ES/SS` — данные, `FS/GS` = 0. Ядро обязано загрузить свою GDT до любого использования сегментов |
| IDT | не определена; ядро загружает свою первым делом |
| Стек | `RSP` выровнен на 16 байт (как перед `call`, т.е. `RSP+8` кратен 16 в точке входа), не меньше 64 КиБ, в отображённой памяти типа `BootloaderReclaimable` |
| Регистры | `RDI` = физический адрес `BootInfo` (System V ABI, первый аргумент); виртуальный ядро вычисляет как `hhdm_base + RDI`; прочие не определены |
| `CR0` | `PE`, `PG`, `WP` установлены; FPU/SSE инициализированы (`CR0.EM=0`, `CR4.OSFXSR=1`, `CR4.OSXMMEXCPT=1`) |
| `EFER` | `LME`, `LMA`, `NXE` установлены |
| Флаг направления | `DF=0` |
| COM1 | инициализирован загрузчиком (115200 8N1), ядро может писать в него сразу |

Точка входа: `extern "C" [[noreturn]] void kernel_entry(uint64_t boot_info_phys);`
Адрес — `e_entry` ELF-файла ядра.

## 3. Формат ядра и что делает загрузчик

- Ядро: ELF64, `ET_EXEC`, `EM_X86_64`, статически слинковано, без
  перемещений. Загрузчик обрабатывает только сегменты `PT_LOAD`; прочие
  игнорирует.
- Каждый `PT_LOAD` загружается в свежие физические страницы (выравнивание 4 КиБ),
  хвост `p_memsz > p_filesz` обнуляется (`.bss`). Права страниц — по `p_flags`
  (W^X: сегмент не может быть одновременно `W` и `X` → ошибка загрузки).
- Виртуальные адреса `PT_LOAD` обязаны лежать в `[kKernelBase, 2^64)`,
  `kKernelBase = 0xffffffff80000000` (верхние 2 ГиБ, `-mcmodel=kernel`).
- Файлы на ESP: `\EFI\BOOT\BOOTX64.EFI` (загрузчик), `\charmos\kernel.elf`,
  `\charmos\cmdline.txt` (необязателен; UTF-8, завершающие CR/LF отбрасываются), `\charmos\initrd` (необязателен;
  загружается как есть в память типа `Initrd`, формат — docs/abi/initrd.md).

## 4. Виртуальная память при входе

Загрузчик строит 4-уровневые таблицы (без LA57) и отображает:

1. **Ядро** — сегменты `PT_LOAD` по их `p_vaddr`, права по `p_flags`.
2. **Прямое отображение физической памяти (HHDM)** — вся физическая память из
   карты памяти (все типы, кроме `Reserved` выше 4 ГиБ) по адресу
   `hhdm_base + phys`, `hhdm_base = 0xffff800000000000`, RW, NX; страницы 2 МиБ
   где возможно.
3. **Тождественное отображение** первых 4 ГиБ — RWX, только чтобы пережить
   переключение CR3 внутри загрузчика. Ядро обязано снять его после установки
   своих таблиц. Помечено в `BootInfo.flags`.
4. **Стек ядра** — внутри HHDM.

Фреймбуфер отображён через HHDM; тип кэширования не гарантирован (ядро
настраивает PAT/WC само).

## 5. Структуры

```cpp
namespace abi::boot {

inline constexpr uint64_t kMagic = 0x544f4f42534d5243;  // "CRMSBOOT" little-endian
inline constexpr uint16_t kVersionMajor = 1;
inline constexpr uint16_t kVersionMinor = 0;
inline constexpr uint64_t kKernelBase = 0xffffffff80000000;
inline constexpr uint64_t kHhdmBase   = 0xffff800000000000;

enum class MemoryType : uint32_t {
  Usable               = 1,  // свободна, ядро распоряжается
  BootloaderReclaimable = 2, // таблицы страниц, стек, BootInfo — свободна после того, как ядро всё скопировало
  KernelImage          = 3,  // сегменты ядра
  Framebuffer          = 4,
  AcpiReclaimable      = 5,  // таблицы ACPI, свободна после разбора
  AcpiNvs              = 6,
  Reserved             = 7,  // никогда не трогать (в т.ч. UEFI Runtime Services)
  BadMemory            = 8,
  Initrd               = 9,
};

struct MemoryRegion {          // 24 байта
  uint64_t base;               // физ., кратно 4096
  uint64_t length;             // байт, кратно 4096
  MemoryType type;
  uint32_t reserved;           // 0
};

enum class PixelFormat : uint32_t {
  Rgbx8888 = 1,  // байты в памяти: R, G, B, x
  Bgrx8888 = 2,  // байты в памяти: B, G, R, x
};

struct Framebuffer {           // 32 байта
  uint64_t base;               // физ.; 0 = фреймбуфера нет
  uint32_t width;              // пикселей
  uint32_t height;
  uint32_t pitch;              // байт на строку (>= width * 4)
  PixelFormat format;
  uint64_t reserved;
};

struct BootInfo {
  uint64_t magic;              // kMagic
  uint16_t version_major;
  uint16_t version_minor;
  uint32_t size;               // sizeof(BootInfo) на стороне загрузчика
  uint64_t flags;              // бит 0: identity map нижних 4 ГиБ присутствует

  uint64_t memory_map;         // физ. адрес массива MemoryRegion, отсортирован по base, без пересечений
  uint64_t memory_map_count;

  uint64_t kernel_phys_base;   // физ. адрес самого нижнего PT_LOAD
  uint64_t kernel_phys_size;   // байт, суммарно занятых образом
  uint64_t hhdm_base;          // = kHhdmBase

  Framebuffer framebuffer;

  uint64_t acpi_rsdp;          // физ. адрес RSDP (ACPI 2.0+, из EFI Configuration Table); 0 = нет
  uint64_t efi_system_table;   // физ.; только для диагностики, Boot Services закрыты

  uint64_t cmdline;            // физ. адрес UTF-8 строки с завершающим 0; 0 = нет
  uint64_t cmdline_length;     // без завершающего 0

  uint64_t initrd_base;        // физ.; 0 = нет (загрузчик кладёт файл как есть; формат — docs/abi/initrd.md)
  uint64_t initrd_size;

  uint64_t boot_time_tsc;      // TSC в момент перед прыжком в ядро
  uint64_t reserved[8];        // 0; место под минорные расширения
};

}  // namespace abi::boot
```

## 6. Ошибки

- Загрузчик при любой ошибке печатает причину в serial и на экран (через UEFI
  ConOut, пока Boot Services открыты), затем стоит в цикле. В режиме тестов —
  выходит через `isa-debug-exit` с кодом `kExitFailure`.
- Ядро первым делом проверяет `magic`, `version_major` и `size`. Несовпадение —
  паника с выводом обоих значений.

## 7. Решения при фиксации v1.0

- `BootInfo` передаётся только физическим адресом в `RDI`; отдельного
  виртуального адреса нет (один источник правды).
- `\charmos\cmdline.txt` читается в M1 — нужен `test.sh` для выбора режимов ядра.
- AP запускает ядро, не загрузчик.
- KASLR — не в v1; добавляется минорной версией через существующие поля
  `hhdm_base` и `kernel_phys_base` плюс новое поле в `reserved`.
