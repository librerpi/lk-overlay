#pragma once
#include <stddef.h>
void i2c_set_rate(unsigned int bus, unsigned long rate);
int i2c_xfer(unsigned int bus, unsigned int addr, char *send, size_t send_len,
             char *recv, size_t recv_len);
