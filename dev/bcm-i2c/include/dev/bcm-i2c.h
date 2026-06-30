#pragma once

#include <stdint.h>

int smbus_write(int nr, int addr, int reg, const uint8_t *buf, int len);
int smbus_read(int nr, int addr, int reg, uint8_t *buf, int len);
