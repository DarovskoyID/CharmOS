#pragma once
// Интерпретатор AML (docs/specs/acpi-6.5.pdf, гл. 20 — грамматика и байты,
// гл. 19 — смысл операторов, §5.3 — пространство имён).
//
// Таблица (DSDT, SSDT) выполняется как список термов: именованные объекты
// создают узлы пространства имён, тела методов пропускаются и исполняются
// при вызове прямо по байтам таблицы (таблица должна жить, пока жив
// интерпретатор). Доступ к оборудованию (поля OperationRegion) — через Host.
//
// Память: узлы и значения, сохранённые в пространстве имён, — в постоянной
// арене; временные значения вычисления — во временной, она сбрасывается
// после каждого evaluate(). Ничего не освобождается по отдельности
// (правило 9: сборка мусора — после замеров).

#include <stddef.h>
#include <stdint.h>

#include <lib/core/string_view.hpp>

namespace lib::acpi::aml {

// Источник памяти арен: кусок не меньше size байт, nullptr — нет памяти.
struct Allocator {
  void* (*alloc)(void* ctx, size_t size);
  void* ctx;
};

// Пространства адресов OperationRegion (табл. 20.2, RegionSpace).
enum class Space : uint8_t {
  SystemMemory = 0,
  SystemIO = 1,
  PciConfig = 2,
  EmbeddedControl = 3,
  SmBus = 4,
  Cmos = 5,
  PciBarTarget = 6,
  Ipmi = 7,
  Gpio = 8,
  GenericSerialBus = 9,
  Pcc = 10,
};

struct PciAddress {
  uint16_t segment;
  uint8_t bus, device, function;
};

// Что интерпретатор просит у среды. width — 8, 16, 32 или 64 бита.
struct Host {
  void* ctx;
  uint64_t (*read)(void* ctx, Space space, uint64_t address, uint32_t width, const PciAddress* pci);
  void (*write)(void* ctx, Space space, uint64_t address, uint32_t width, uint64_t value, const PciAddress* pci);
  void (*log)(void* ctx, const char* message);
  void (*sleep_ms)(void* ctx, uint64_t ms);
  void (*stall_us)(void* ctx, uint64_t us);
};

enum class Error : uint8_t {
  Ok,
  NoMemory,
  BadTable,       // заголовок/длина
  BadOpcode,      // неизвестный или неподдержанный код
  Truncated,      // выход за конец тела
  NotFound,       // имя не найдено
  BadType,        // операнд не того типа
  BadArgs,        // число аргументов метода
  DivideByZero,
  OutOfRange,     // индекс
  Fatal,          // FatalOp
  TooDeep,        // вложенность вызовов
  Unsupported,    // пространство адресов, LoadTable и т. п.
};
const char* error_name(Error e);

struct Node;
struct Object;

// Значение. Узлы-контейнеры (Device и пр.) значений не имеют.
enum class Type : uint8_t {
  Uninitialized = 0,
  Integer = 1,
  String = 2,
  Buffer = 3,
  Package = 4,
  FieldUnit = 5,
  Device = 6,
  Event = 7,
  Method = 8,
  Mutex = 9,
  OpRegion = 10,
  PowerResource = 11,
  Processor = 12,
  ThermalZone = 13,
  BufferField = 14,
  Debug = 16,
  // Внутренние (не видны ObjectType): ссылка, необработанное имя в пакете, Scope.
  Reference = 0x40,
  NameRef = 0x41,
  Scope = 0x42,
};

struct Value {
  Type type;
  uint64_t integer;      // Integer
  const char* str;       // String (с нулём), длина в size
  uint8_t* data;         // Buffer
  Value* items;          // Package
  uint64_t size;         // байт строки/буфера или число элементов пакета
  Node* node;            // Reference на именованный объект, NameRef (если найден)
  Value* target;         // Reference на элемент пакета / локальную переменную
};

struct Node {
  char name[4];
  Node* parent;
  Node* child;
  Node* next;
  Object* object;  // nullptr — Scope без объекта
};

class Interpreter {
 public:
  void init(Allocator permanent, Allocator scratch, Host host);

  // Загружает определение (DSDT или SSDT целиком, с заголовком).
  Error load_table(const uint8_t* table, size_t size);

  // Абсолютный путь вида "\\_SB.PCI0._PRT" или относительный от scope.
  Node* find(lib::core::StringView path, Node* scope = nullptr) const;
  Node* root() const { return root_; }

  // Вычислить объект: метод вызывается с args, остальное читается. Результат
  // копируется в постоянную арену (пока результаты нужны недолго — правило 9)
  // и остаётся действительным до следующего evaluate().
  Error evaluate(Node* node, const Value* args, uint32_t argc, Value* out);
  Error evaluate(lib::core::StringView path, Value* out, Node* scope = nullptr);

  // Вызов для каждого узла поддерева (в глубину). Остановка — fn вернул false.
  void walk(Node* from, bool (*fn)(Node* n, void* ctx), void* ctx) const;
  Type type_of(const Node* n) const;

  // Полный путь узла (\_SB_.PCI0) в buf.
  size_t path_of(const Node* n, char* buf, size_t cap) const;

  // Счётчики для тестов.
  uint32_t node_count() const { return nodes_; }
  uint32_t method_count() const { return methods_; }
  uint32_t integer_width() const { return int_bits_; }
  // Смещение в таблице, где остановилась последняя неудачная загрузка.
  size_t error_offset() const { return error_offset_; }

  // Внутреннее (используется реализацией).
  struct Arena {
    Allocator src{};
    uint8_t* cur = nullptr;
    size_t left = 0;
    struct Chunk* chunks = nullptr;   // для повторного использования после reset
    struct Chunk* used = nullptr;
    void* get(size_t size);
    void reset();
  };
  Arena perm_{}, scratch_{};
  Host host_{};
  Node* root_ = nullptr;
  uint32_t nodes_ = 0, methods_ = 0;
  uint32_t int_bits_ = 64;
  uint32_t depth_ = 0;
  size_t error_offset_ = 0;
};

}  // namespace lib::acpi::aml
