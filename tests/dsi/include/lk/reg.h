#pragma once
#include <stdint.h>
uint32_t *test_reg(uint32_t address);
#define REG32(address) test_reg(address)
