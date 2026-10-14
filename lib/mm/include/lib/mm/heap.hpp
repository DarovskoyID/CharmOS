#pragma once
// Куча ядра: блоки с заголовком и концевиком (boundary tags), двусвязный
// список свободных блоков, first-fit, слияние соседей при освобождении.
// Выравнивание всех выделений — 16 байт.
//
// Раскладка области [base, base + mapped):
//   [пролог 8 Б: занятый концевик] [блоки ...] [эпилог 8 Б: заголовок size=0, занят]
// Блок: [заголовок 8 Б: size|used] [данные] [концевик 8 Б: копия заголовка],
// size включает заголовок и концевик и кратен 16. Блоки начинаются по адресу
// ≡ 8 (mod 16), поэтому данные выровнены на 16. В свободном блоке в данных
// лежат указатели next/prev списка, отсюда минимальный размер блока — 32 Б.
//
// Память область получает через grow(): вызывающий отображает новые страницы.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>

namespace lib::mm {

enum class HeapError : uint8_t {
  OutOfMemory,     // grow() отказал или исчерпан max_size
  InvalidArgument,
  InvalidPointer,  // free() указателя, который куча не выдавала
  Corrupted,       // заголовок и концевик не совпадают
};

class Heap {
 public:
  // Отобразить [at, at + bytes); bytes кратен kGrowStep.
  using GrowFn = bool (*)(void* ctx, uintptr_t at, size_t bytes);

  static constexpr size_t kAlign = 16;
  static constexpr size_t kGrowStep = 64 * 1024;
  static constexpr size_t kMinBlock = 32;

  // base выровнен на 4 КиБ; max_size — предел области (кратен kGrowStep).
  lib::core::Status<HeapError> init(uintptr_t base, size_t max_size, GrowFn grow, void* ctx);

  lib::core::Result<void*, HeapError> alloc(size_t size);
  lib::core::Status<HeapError> free(void* ptr);

  // Полная проверка структуры (для тестов и отладки).
  bool check() const;

  size_t mapped_bytes() const { return mapped_; }
  size_t used_bytes() const { return used_; }
  size_t free_list_length() const;

 private:
  struct FreeNode {
    FreeNode* next;
    FreeNode* prev;
  };

  static uint64_t& header(uintptr_t block) { return *reinterpret_cast<uint64_t*>(block); }
  static uint64_t& footer(uintptr_t block, size_t size) {
    return *reinterpret_cast<uint64_t*>(block + size - 8);
  }
  static size_t size_of(uint64_t tag) { return static_cast<size_t>(tag & ~uint64_t{15}); }
  static bool used(uint64_t tag) { return (tag & 1) != 0; }
  static void set_tags(uintptr_t block, size_t size, bool is_used);
  static FreeNode* node(uintptr_t block) { return reinterpret_cast<FreeNode*>(block + 8); }
  static uintptr_t block_of(const FreeNode* n) { return reinterpret_cast<uintptr_t>(n) - 8; }

  void list_push(uintptr_t block);
  void list_remove(uintptr_t block);
  // Сливает свободный block с соседями; возвращает итоговый блок.
  uintptr_t coalesce(uintptr_t block);
  bool grow(size_t at_least);

  uintptr_t base_ = 0;
  size_t max_ = 0;
  size_t mapped_ = 0;
  size_t used_ = 0;
  GrowFn grow_fn_ = nullptr;
  void* ctx_ = nullptr;
  FreeNode* free_head_ = nullptr;
};

}  // namespace lib::mm
