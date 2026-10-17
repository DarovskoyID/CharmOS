#pragma once
// TPM 2.0 в ядре (журнал решений 2026-10-15): интерфейс CRB по TCG PTP 1.07
// (§6.5.3, локальность 0 по 0xFED4_0000) и несколько команд TPM 2.0 Library
// (Part 3) — ровно столько, сколько нужно ключу закрытой области
// (docs/abi/sysarea-crypt.md §3). Ключ из TPM в юзерспейс не уходит.

#include <stddef.h>
#include <stdint.h>

namespace kernel::sync { class Mutex; }

namespace kernel::tpm {

// Есть ли TPM с активным интерфейсом CRB (FIFO/TIS пока нет — M9b).
bool init();
bool present();
// After the scheduler starts, callers serialize complete transactions with
// this mutex. init() runs before any user or driver process exists.
sync::Mutex& runtime_mutex();

// Случайные байты (TPM2_GetRandom).
bool get_random(uint8_t* out, size_t n);

// Запечатать data (до 128 байт) под политику PCR pcr_mask банка SHA-256.
// Результат — TPM2B_PUBLIC | TPM2B_PRIVATE как отдаёт TPM2_Create.
struct Sealed {
  uint8_t pub[512];
  uint16_t pub_size;
  uint8_t priv[512];
  uint16_t priv_size;
};
bool seal(const uint8_t* data, size_t n, uint32_t pcr_mask, Sealed* out);
// Распечатать (политику PCR проверяет TPM); out — не меньше n байт.
bool unseal(const uint8_t* pub, uint16_t pub_size, const uint8_t* priv, uint16_t priv_size, uint32_t pcr_mask,
            uint8_t* out, size_t n);

// Код ответа последней неудачной команды (TPM_RC, 0 — не TPM).
uint32_t last_error();

}  // namespace kernel::tpm
