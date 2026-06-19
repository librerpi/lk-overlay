#pragma once

#include <stdint.h>

//Fixed<uint32_t,12,20> computePllDivisor(uint32_t xtal, uint32_t goal);
//Fixed<uint32_t,16,6> computePL011Divisor(uint32_t refclk_in, uint32_t baud);

#ifdef __cplusplus

extern "C" {
#endif
  uint32_t computePllDivisor(uint32_t xtal, uint32_t goal);
#ifdef __cplusplus
}
#endif
