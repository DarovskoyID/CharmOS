#ifndef _CHARM_MBSTATE_T
#define _CHARM_MBSTATE_T
#include <stdint.h>
typedef struct {
  uint32_t value, minimum;
  uint8_t remaining, total;
  uint16_t reserved;
} mbstate_t;
#endif
