#pragma once

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

void spi_init(void);
void spi_begin(void);
void spi_end(void);
void spi_flash_read_data(uint8_t *buffer, uint32_t offset, size_t length);
ssize_t spi_read_file(const char *filename, uint8_t **buffer);
