#pragma once
// Ed25519 (RFC 8032 §5.1, PureEdDSA). verify — для ОС (служба пакетов),
// keypair и sign — для хост-инструмента tools/sign.
//
// Скалярное умножение не постоянного времени: подпись делается только на
// машине автора (tools/sign), в ОС — только проверка открытых данных.

#include <stddef.h>
#include <stdint.h>

namespace lib::crypto {

inline constexpr size_t kEd25519SeedSize = 32;
inline constexpr size_t kEd25519PublicSize = 32;
inline constexpr size_t kEd25519SignatureSize = 64;

// §5.1.5: открытый ключ из 32-байтного секрета.
void ed25519_public_key(const uint8_t seed[kEd25519SeedSize], uint8_t pub[kEd25519PublicSize]);
// §5.1.6
void ed25519_sign(const uint8_t seed[kEd25519SeedSize], const void* msg, size_t len,
                  uint8_t sig[kEd25519SignatureSize]);
// §5.1.7 (проверка без умножения на кофактор: [S]B = R + [k]A).
bool ed25519_verify(const uint8_t pub[kEd25519PublicSize], const void* msg, size_t len,
                    const uint8_t sig[kEd25519SignatureSize]);

}  // namespace lib::crypto
