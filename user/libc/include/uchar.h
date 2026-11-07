#ifndef _UCHAR_H
#define _UCHAR_H
/* C11 Unicode character types. Chromium/ICU require this header to exist;
   CharmOS libc currently exposes only the types (no conversion functions). */
#include <stddef.h>
#include <stdint.h>

#ifndef __cplusplus
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#endif

#endif
