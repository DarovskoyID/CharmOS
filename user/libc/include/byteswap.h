#ifndef _BYTESWAP_H
#define _BYTESWAP_H
#include <stdint.h>
#define bswap_16(x) __builtin_bswap16((uint16_t)(x))
#define bswap_32(x) __builtin_bswap32((uint32_t)(x))
#define bswap_64(x) __builtin_bswap64((uint64_t)(x))
#endif
