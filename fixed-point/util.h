#pragma once

#include <stdint.h>

Fixed<uint32_t,12,20> computePllDivisor(uint32_t xtal, uint32_t goal);
Fixed<uint32_t,16,6> computePL011Divisor(uint32_t refclk_in, uint32_t baud);
